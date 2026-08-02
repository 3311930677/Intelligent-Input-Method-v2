#include "owo/plugin/plugin_store.h"
#include "owo/plugin/plugin_authorization_store.h"

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#endif

#include <algorithm>
#include <fstream>
#include <limits>
#include <map>
#include <vector>

namespace owo::plugin {
namespace {

constexpr std::string_view kRecordName = "install.record";

bool sha256_text(const std::string_view value) {
    return value.size() == 64 && std::all_of(value.begin(), value.end(), [](const unsigned char byte) {
        return (byte >= '0' && byte <= '9') || (byte >= 'a' && byte <= 'f');
    });
}

bool plugin_id_text(const std::string_view value) {
    if (value.size() < 3 || value.size() > 128 || value.front() == '.' ||
        value.back() == '.' || value.find('.') == std::string_view::npos) return false;
    bool previous_dot = false;
    for (const unsigned char byte : value) {
        const bool valid = (byte >= 'a' && byte <= 'z') || (byte >= '0' && byte <= '9') ||
                           byte == '-' || byte == '.';
        if (!valid || (byte == '.' && previous_dot)) return false;
        previous_dot = byte == '.';
    }
    return true;
}

bool version_text(const std::string_view value) {
    unsigned parts = 0;
    std::size_t start = 0;
    while (start < value.size()) {
        const auto end = value.find('.', start);
        const auto token = value.substr(start, end == std::string_view::npos
            ? value.size() - start : end - start);
        if (token.empty() || (token.size() > 1 && token.front() == '0') ||
            !std::all_of(token.begin(), token.end(), [](const unsigned char byte) {
                return byte >= '0' && byte <= '9';
            })) return false;
        ++parts;
        if (end == std::string_view::npos) break;
        start = end + 1;
    }
    return parts == 3;
}

#ifdef _WIN32
bool safe_directory(const std::filesystem::path& path) {
    const auto attributes = GetFileAttributesW(path.c_str());
    return attributes != INVALID_FILE_ATTRIBUTES &&
           (attributes & FILE_ATTRIBUTE_DIRECTORY) != 0 &&
           (attributes & FILE_ATTRIBUTE_REPARSE_POINT) == 0;
}

bool safe_file(const std::filesystem::path& path) {
    const auto attributes = GetFileAttributesW(path.c_str());
    return attributes != INVALID_FILE_ATTRIBUTES &&
           (attributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)) == 0;
}

bool ensure_directory(const std::filesystem::path& path) {
    if (CreateDirectoryW(path.c_str(), nullptr) == FALSE && GetLastError() != ERROR_ALREADY_EXISTS)
        return false;
    return safe_directory(path);
}

bool safe_local_parent(const std::filesystem::path& parent) {
    if (!parent.is_absolute() || parent.root_name().native().size() != 2 ||
        parent.root_name().native()[1] != L':') return false;
    auto current = parent.root_path();
    for (const auto& component : parent.relative_path()) {
        current /= component;
        if (!safe_directory(current)) return false;
    }
    return true;
}

std::string windows_error(const char* prefix) {
    return std::string(prefix) + " (Windows error " + std::to_string(GetLastError()) + ")";
}

bool write_exact(HANDLE file, const std::string_view bytes) {
    std::size_t offset = 0;
    while (offset < bytes.size()) {
        DWORD written = 0;
        const auto chunk = static_cast<DWORD>((std::min)(bytes.size() - offset,
            static_cast<std::size_t>((std::numeric_limits<DWORD>::max)())));
        if (!WriteFile(file, bytes.data() + offset, chunk, &written, nullptr) || written != chunk)
            return false;
        offset += written;
    }
    return FlushFileBuffers(file) != FALSE;
}

bool atomic_write(const std::filesystem::path& target, const std::string_view bytes) {
    const auto existing_attributes = GetFileAttributesW(target.c_str());
    if (existing_attributes != INVALID_FILE_ATTRIBUTES && !safe_file(target)) return false;
    const auto suffix = L".tmp." + std::to_wstring(GetCurrentProcessId()) + L"." +
                        std::to_wstring(GetTickCount64());
    auto temporary = target;
    temporary += suffix;
    HANDLE file = CreateFileW(temporary.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW,
                              FILE_ATTRIBUTE_NORMAL | FILE_FLAG_WRITE_THROUGH, nullptr);
    if (file == INVALID_HANDLE_VALUE) return false;
    const bool written = write_exact(file, bytes);
    CloseHandle(file);
    if (!written || MoveFileExW(temporary.c_str(), target.c_str(),
                                MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) == FALSE) {
        DeleteFileW(temporary.c_str());
        return false;
    }
    return true;
}
#endif

std::string serialize_record(const PluginManifest& manifest, const std::string_view inventory,
                             const std::string_view certificate) {
    return "schema_version=1\nplugin_id=" + manifest.id + "\nversion=" + manifest.version +
           "\ninventory_sha256=" + std::string(inventory) +
           "\npublisher_certificate_sha256=" + std::string(certificate) + "\n";
}

struct Record {
    std::string plugin_id;
    std::string version;
    std::string inventory;
    std::string certificate;
};

bool same_record(const Record& left, const Record& right) {
    return left.plugin_id == right.plugin_id && left.version == right.version &&
           left.inventory == right.inventory && left.certificate == right.certificate;
}

bool read_record(const std::filesystem::path& path, Record& result) {
#ifdef _WIN32
    if (!safe_file(path)) return false;
#endif
    std::error_code error;
    const auto size = std::filesystem::file_size(path, error);
    if (error || size == 0 || size > 1024) return false;
    std::ifstream input(path, std::ios::binary);
    std::string bytes(static_cast<std::size_t>(size), '\0');
    if (!input.read(bytes.data(), static_cast<std::streamsize>(bytes.size()))) return false;
    std::map<std::string, std::string> fields;
    std::size_t offset = 0;
    while (offset < bytes.size()) {
        const auto end = bytes.find('\n', offset);
        if (end == std::string::npos) return false;
        const auto line = std::string_view(bytes).substr(offset, end - offset);
        const auto separator = line.find('=');
        if (separator == std::string_view::npos || separator == 0 || separator + 1 == line.size() ||
            !fields.emplace(std::string(line.substr(0, separator)),
                            std::string(line.substr(separator + 1))).second) return false;
        offset = end + 1;
    }
    if (fields.size() != 5 || fields["schema_version"] != "1" ||
        !plugin_id_text(fields["plugin_id"]) || !version_text(fields["version"]) ||
        !sha256_text(fields["inventory_sha256"]) ||
        !sha256_text(fields["publisher_certificate_sha256"])) return false;
    result = {fields["plugin_id"], fields["version"], fields["inventory_sha256"],
              fields["publisher_certificate_sha256"]};
    return true;
}

PluginStoreResult failure(std::string diagnostic) {
    return {false, {}, {}, {}, std::move(diagnostic)};
}

#ifdef _WIN32
bool direct_children(const std::filesystem::path& directory,
                     std::vector<std::filesystem::path>& children,
                     std::string& diagnostic) {
    std::error_code error;
    std::filesystem::directory_iterator iterator(directory, error);
    const std::filesystem::directory_iterator end;
    while (!error && iterator != end) {
        children.push_back(iterator->path());
        iterator.increment(error);
    }
    if (error) {
        diagnostic = "cannot enumerate plugin store directory";
        return false;
    }
    std::sort(children.begin(), children.end());
    return true;
}

bool read_small_file(const std::filesystem::path& path, const std::uintmax_t maximum,
                     std::string& bytes) {
    if (!safe_file(path)) return false;
    std::error_code error;
    const auto size = std::filesystem::file_size(path, error);
    if (error || size == 0 || size > maximum) return false;
    std::ifstream input(path, std::ios::binary);
    bytes.assign(static_cast<std::size_t>(size), '\0');
    return input.read(bytes.data(), static_cast<std::streamsize>(bytes.size())) &&
           input.peek() == std::char_traits<char>::eof();
}

void add_recovery_item(PluginRecoveryScanResult& result, const PluginRecoveryKind kind,
                       const std::filesystem::path& path, std::string diagnostic,
                       std::string plugin_id = {}, std::string version = {}) {
    result.items.push_back({kind, path, std::move(plugin_id), std::move(version),
                            std::move(diagnostic)});
}

bool valid_installed_version(const std::filesystem::path& root, const Record& record) {
    const auto installed = root / L"versions" / std::filesystem::path(record.plugin_id) /
                           std::filesystem::path(record.version);
    if (!safe_directory(installed)) return false;
    const auto manifest_path = installed / L"manifest.json";
    if (!safe_file(manifest_path)) return false;
    const auto manifest = load_manifest(manifest_path);
    if (!manifest.ok || manifest.value.id != record.plugin_id ||
        manifest.value.version != record.version) return false;
    Record version_record;
    const auto record_path = root / L"records" / std::filesystem::path(record.plugin_id) /
                             std::filesystem::path(record.version + ".record");
    return read_record(record_path, version_record) && same_record(record, version_record);
}
#endif

}  // namespace

PluginStoreResult initialize_plugin_store(const std::filesystem::path& root) {
#ifdef _WIN32
    if (!root.is_absolute() || root == root.root_path() || !safe_local_parent(root.parent_path()))
        return failure("plugin store must be a local absolute child of a safe existing parent");
    if (!ensure_directory(root)) return failure(windows_error("cannot create safe plugin store root"));
    for (const auto* child : {L"versions", L"records", L"active", L"authorizations",
                              L"data", L"staging"}) {
        if (!ensure_directory(root / child)) return failure(windows_error("cannot create plugin store layout"));
    }
    return {true, {}, root, {}, {}};
#else
    static_cast<void>(root);
    return failure("plugin store is currently available on Windows only");
#endif
}

PluginStoreResult publish_staged_plugin(
    const std::filesystem::path& root, const std::filesystem::path& staging_directory,
    const std::string_view inventory_sha256, const std::string_view publisher_certificate_sha256) {
#ifdef _WIN32
    const auto initialized = initialize_plugin_store(root);
    if (!initialized.ok) return initialized;
    if (!sha256_text(inventory_sha256) || !sha256_text(publisher_certificate_sha256))
        return failure("installation digests must be lowercase SHA-256");
    const auto normalized_staging = std::filesystem::absolute(staging_directory).lexically_normal();
    if (normalized_staging.parent_path() != (root / L"staging").lexically_normal() ||
        !safe_directory(normalized_staging))
        return failure("staging directory must be a safe direct child of the plugin store staging root");
    const auto manifest_path = normalized_staging / L"manifest.json";
    if (!safe_file(manifest_path)) return failure("staged manifest is missing or unsafe");
    const auto manifest = load_manifest(manifest_path);
    if (!manifest.ok) return failure("staged manifest is invalid: " + manifest.diagnostic);
    if (!safe_file(normalized_staging / std::filesystem::path(manifest.value.entry)) ||
        !safe_file(normalized_staging / std::filesystem::path(manifest.value.config_schema)))
        return failure("staged entry point or configuration schema is missing or unsafe");
    const auto versions_for_plugin = root / L"versions" / std::filesystem::path(manifest.value.id);
    const auto records_for_plugin = root / L"records" / std::filesystem::path(manifest.value.id);
    if (!ensure_directory(versions_for_plugin) || !ensure_directory(records_for_plugin) ||
        !ensure_directory(root / L"data" / std::filesystem::path(manifest.value.id)))
        return failure(windows_error("cannot create plugin-specific store directories"));
    const auto destination = versions_for_plugin / std::filesystem::path(manifest.value.version);
    if (GetFileAttributesW(destination.c_str()) != INVALID_FILE_ATTRIBUTES)
        return failure("plugin version is already installed");
    Record previous;
    const auto active_path = root / L"active" / std::filesystem::path(manifest.value.id + ".record");
    read_record(active_path, previous);
    if (MoveFileExW(normalized_staging.c_str(), destination.c_str(), MOVEFILE_WRITE_THROUGH) == FALSE)
        return failure(windows_error("cannot atomically publish staged plugin version"));
    const auto record_bytes = serialize_record(manifest.value, inventory_sha256,
                                               publisher_certificate_sha256);
    const auto version_record = records_for_plugin / std::filesystem::path(manifest.value.version + ".record");
    if (!atomic_write(version_record, record_bytes)) {
        const bool rolled_back = MoveFileExW(destination.c_str(), normalized_staging.c_str(),
                                             MOVEFILE_WRITE_THROUGH) != FALSE;
        auto result = failure(windows_error("cannot durably record installed plugin version"));
        result.manifest = manifest.value;
        result.installed_path = rolled_back ? std::filesystem::path{} : destination;
        result.previous_version = previous.version;
        result.version_published = !rolled_back;
        return result;
    }
    if (!atomic_write(active_path, record_bytes)) {
        auto result = failure(windows_error("plugin version was installed but could not be activated"));
        result.manifest = manifest.value;
        result.installed_path = destination;
        result.previous_version = previous.version;
        result.version_published = true;
        return result;
    }
    return {true, manifest.value, destination, previous.version, {}, true, true};
#else
    static_cast<void>(root); static_cast<void>(staging_directory);
    static_cast<void>(inventory_sha256); static_cast<void>(publisher_certificate_sha256);
    return failure("plugin store is currently available on Windows only");
#endif
}

PluginStoreResult activate_installed_plugin_version(
    const std::filesystem::path& root, const std::string_view plugin_id,
    const std::string_view version) {
#ifdef _WIN32
    const auto initialized = initialize_plugin_store(root);
    if (!initialized.ok) return initialized;
    if (!plugin_id_text(plugin_id) || !version_text(version))
        return failure("requested plugin id or version is invalid");
    const auto installed = root / L"versions" / std::filesystem::path(plugin_id) /
                           std::filesystem::path(version);
    const auto manifest_path = installed / L"manifest.json";
    if (!safe_directory(installed) || !safe_file(manifest_path))
        return failure("requested installed plugin version is invalid");
    const auto manifest = load_manifest(manifest_path);
    if (!manifest.ok || manifest.value.id != plugin_id || manifest.value.version != version)
        return failure("requested installed plugin version is invalid");
    Record record;
    const auto version_record = root / L"records" / std::filesystem::path(plugin_id) /
                                std::filesystem::path(std::string(version) + ".record");
    if (!read_record(version_record, record) || record.plugin_id != plugin_id || record.version != version)
        return failure("installed plugin version record is invalid");
    Record previous;
    const auto active_path = root / L"active" / std::filesystem::path(std::string(plugin_id) + ".record");
    read_record(active_path, previous);
    if (!atomic_write(active_path, serialize_record(manifest.value, record.inventory, record.certificate)))
        return failure(windows_error("cannot atomically activate installed plugin version"));
    return {true, manifest.value, installed, previous.version, {}, true, true};
#else
    static_cast<void>(root); static_cast<void>(plugin_id); static_cast<void>(version);
    return failure("plugin store is currently available on Windows only");
#endif
}

InstalledPluginVersionResult query_installed_plugin_version(
    const std::filesystem::path& root, const std::string_view plugin_id,
    const std::string_view version) {
#ifdef _WIN32
    const auto store_root = root.lexically_normal();
    if (!store_root.is_absolute() || store_root == store_root.root_path() ||
        !safe_local_parent(store_root.parent_path()) || !safe_directory(store_root) ||
        !safe_directory(store_root / L"versions") || !safe_directory(store_root / L"records"))
        return {false, {}, {}, {}, {}, "plugin store root or binding layout is unsafe"};
    if (!plugin_id_text(plugin_id) || !version_text(version))
        return {false, {}, {}, {}, {}, "requested plugin id or version is invalid"};
    const auto installed = store_root / L"versions" / std::filesystem::path(plugin_id) /
                           std::filesystem::path(version);
    const auto manifest_path = installed / L"manifest.json";
    if (!safe_directory(installed) || !safe_file(manifest_path))
        return {false, {}, {}, {}, {}, "installed plugin version is missing or unsafe"};
    const auto manifest = load_manifest(manifest_path);
    if (!manifest.ok || manifest.value.id != plugin_id || manifest.value.version != version)
        return {false, {}, {}, {}, {}, "installed manifest does not match requested identity"};
    Record record;
    const auto record_path = store_root / L"records" / std::filesystem::path(plugin_id) /
                             std::filesystem::path(std::string(version) + ".record");
    if (!read_record(record_path, record) || record.plugin_id != plugin_id ||
        record.version != version)
        return {false, {}, {}, {}, {}, "installed version record is missing or invalid"};
    return {true, manifest.value, installed, record.inventory, record.certificate, {}};
#else
    static_cast<void>(root); static_cast<void>(plugin_id); static_cast<void>(version);
    return {false, {}, {}, {}, {},
            "installed plugin version queries are currently available on Windows only"};
#endif
}

InstalledPluginVersionResult query_active_plugin_version(
    const std::filesystem::path& root, const std::string_view plugin_id) {
#ifdef _WIN32
    const auto store_root = root.lexically_normal();
    if (!store_root.is_absolute() || store_root == store_root.root_path() ||
        !safe_local_parent(store_root.parent_path()) || !safe_directory(store_root) ||
        !safe_directory(store_root / L"active"))
        return {false, {}, {}, {}, {}, "plugin store root or active layout is unsafe"};
    if (!plugin_id_text(plugin_id))
        return {false, {}, {}, {}, {}, "requested plugin id is invalid"};
    Record record;
    const auto active_path = store_root / L"active" /
        std::filesystem::path(std::string(plugin_id) + ".record");
    if (!read_record(active_path, record) || record.plugin_id != plugin_id)
        return {false, {}, {}, {}, {}, "active plugin record is missing or invalid"};
    auto installed = query_installed_plugin_version(
        store_root, record.plugin_id, record.version);
    if (!installed.ok || installed.inventory_sha256 != record.inventory ||
        installed.publisher_certificate_sha256 != record.certificate)
        return {false, {}, {}, {}, {},
                "active plugin record does not match the installed version binding"};
    return installed;
#else
    static_cast<void>(root); static_cast<void>(plugin_id);
    return {false, {}, {}, {}, {},
            "active plugin version queries are currently available on Windows only"};
#endif
}

PluginRecoveryScanResult scan_plugin_store_recovery(const std::filesystem::path& root) {
#ifdef _WIN32
    PluginRecoveryScanResult result{true, {}, {}};
    const auto store_root = root.lexically_normal();
    if (!store_root.is_absolute() || store_root == store_root.root_path() ||
        !safe_local_parent(store_root.parent_path()))
        return {false, {}, "plugin store must be a local absolute child of a safe existing parent"};
    const auto root_attributes = GetFileAttributesW(store_root.c_str());
    if (root_attributes == INVALID_FILE_ATTRIBUTES) {
        const auto error = GetLastError();
        if (error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND) return result;
        return {false, {}, windows_error("cannot inspect plugin store root")};
    }
    if (!safe_directory(store_root))
        return {false, {}, "plugin store root is unsafe"};
    for (const auto* child : {L"versions", L"records", L"active", L"authorizations",
                              L"data", L"staging"}) {
        if (!safe_directory(store_root / child))
            return {false, {}, "plugin store layout is missing or unsafe"};
    }

    std::vector<std::filesystem::path> active_paths;
    if (!direct_children(store_root / L"active", active_paths, result.diagnostic)) {
        result.ok = false;
        return result;
    }
    std::map<std::string, Record> active_records;
    for (const auto& path : active_paths) {
        Record record;
        if (!read_record(path, record) ||
            path != store_root / L"active" / std::filesystem::path(record.plugin_id + ".record") ||
            !valid_installed_version(store_root, record)) {
            add_recovery_item(result, PluginRecoveryKind::invalid_active_record, path,
                              "active record is unsafe, malformed, or does not resolve to a matching installed version");
            continue;
        }
        active_records.emplace(record.plugin_id, std::move(record));
    }

    std::vector<std::filesystem::path> plugin_directories;
    if (!direct_children(store_root / L"versions", plugin_directories, result.diagnostic)) {
        result.ok = false;
        return result;
    }
    for (const auto& plugin_directory : plugin_directories) {
        if (!safe_directory(plugin_directory)) {
            add_recovery_item(result, PluginRecoveryKind::unsafe_store_entry, plugin_directory,
                              "versions entry is not a safe directory");
            continue;
        }
        std::vector<std::filesystem::path> version_directories;
        if (!direct_children(plugin_directory, version_directories, result.diagnostic)) {
            result.ok = false;
            return result;
        }
        for (const auto& version_directory : version_directories) {
            if (!safe_directory(version_directory)) {
                add_recovery_item(result, PluginRecoveryKind::unsafe_store_entry, version_directory,
                                  "version entry is not a safe directory");
                continue;
            }
            const auto manifest_path = version_directory / L"manifest.json";
            const auto manifest = safe_file(manifest_path) ? load_manifest(manifest_path)
                                                           : ManifestResult{};
            Record version_record;
            bool valid = manifest.ok;
            if (valid) {
                const auto expected_path = store_root / L"versions" /
                    std::filesystem::path(manifest.value.id) /
                    std::filesystem::path(manifest.value.version);
                const auto record_path = store_root / L"records" /
                    std::filesystem::path(manifest.value.id) /
                    std::filesystem::path(manifest.value.version + ".record");
                valid = version_directory == expected_path && read_record(record_path, version_record) &&
                        version_record.plugin_id == manifest.value.id &&
                        version_record.version == manifest.value.version;
            }
            if (!valid) {
                add_recovery_item(result, PluginRecoveryKind::orphaned_version, version_directory,
                                  "published version has no matching valid manifest and version record",
                                  manifest.ok ? manifest.value.id : std::string{},
                                  manifest.ok ? manifest.value.version : std::string{});
                continue;
            }
            const auto active = active_records.find(version_record.plugin_id);
            if (active == active_records.end() || !same_record(active->second, version_record)) {
                add_recovery_item(result, PluginRecoveryKind::inactive_version, version_directory,
                                  "installed version is not the valid active version",
                                  version_record.plugin_id, version_record.version);
            }
        }
    }

    std::vector<std::filesystem::path> record_plugin_directories;
    if (!direct_children(store_root / L"records", record_plugin_directories, result.diagnostic)) {
        result.ok = false;
        return result;
    }
    for (const auto& plugin_directory : record_plugin_directories) {
        if (!safe_directory(plugin_directory)) {
            add_recovery_item(result, PluginRecoveryKind::unsafe_store_entry, plugin_directory,
                              "records entry is not a safe directory");
            continue;
        }
        std::vector<std::filesystem::path> record_paths;
        if (!direct_children(plugin_directory, record_paths, result.diagnostic)) {
            result.ok = false;
            return result;
        }
        for (const auto& path : record_paths) {
            Record record;
            if (!read_record(path, record) ||
                path != store_root / L"records" / std::filesystem::path(record.plugin_id) /
                        std::filesystem::path(record.version + ".record") ||
                !valid_installed_version(store_root, record)) {
                add_recovery_item(result, PluginRecoveryKind::orphaned_record, path,
                                  "version record is unsafe, malformed, or has no matching installed version");
            }
        }
    }


    std::vector<std::filesystem::path> authorization_plugin_directories;
    if (!direct_children(store_root / L"authorizations", authorization_plugin_directories,
                         result.diagnostic)) {
        result.ok = false;
        return result;
    }
    for (const auto& plugin_directory : authorization_plugin_directories) {
        if (!safe_directory(plugin_directory)) {
            add_recovery_item(result, PluginRecoveryKind::unsafe_store_entry, plugin_directory,
                              "authorizations entry is not a safe directory");
            continue;
        }
        std::vector<std::filesystem::path> authorization_paths;
        if (!direct_children(plugin_directory, authorization_paths, result.diagnostic)) {
            result.ok = false;
            return result;
        }
        for (const auto& path : authorization_paths) {
            std::string bytes;
            const auto parsed = read_small_file(path, 2048, bytes)
                ? parse_plugin_authorization(bytes) : PluginAuthorizationResult{};
            bool valid = parsed.ok;
            if (valid) {
                const auto expected_path = store_root / L"authorizations" /
                    std::filesystem::path(parsed.value.plugin_id) /
                    std::filesystem::path(parsed.value.version + ".record");
                const auto loaded = load_plugin_authorization(
                    store_root, parsed.value.plugin_id, parsed.value.version);
                valid = path == expected_path && loaded.ok;
            }
            if (!valid) {
                add_recovery_item(result, PluginRecoveryKind::orphaned_authorization, path,
                                  "authorization record is unsafe, malformed, or does not match an installed binding",
                                  parsed.ok ? parsed.value.plugin_id : std::string{},
                                  parsed.ok ? parsed.value.version : std::string{});
            }
        }
    }

    std::vector<std::filesystem::path> staging_paths;
    if (!direct_children(store_root / L"staging", staging_paths, result.diagnostic)) {
        result.ok = false;
        return result;
    }
    for (const auto& path : staging_paths) {
        if (safe_directory(path) && path.filename().native().starts_with(L".install-")) {
            add_recovery_item(result, PluginRecoveryKind::retained_staging, path,
                              "installation transaction staging directory was retained");
        } else {
            add_recovery_item(result, PluginRecoveryKind::unsafe_store_entry, path,
                              "staging entry is not a recognized safe installation transaction directory");
        }
    }
    std::sort(result.items.begin(), result.items.end(), [](const auto& left, const auto& right) {
        if (left.path != right.path) return left.path < right.path;
        return left.kind < right.kind;
    });
    return result;
#else
    static_cast<void>(root);
    return {false, {}, "plugin store recovery scan is currently available on Windows only"};
#endif
}

}  // namespace owo::plugin
