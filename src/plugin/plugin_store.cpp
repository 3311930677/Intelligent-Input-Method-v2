#include "owo/plugin/plugin_store.h"

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

namespace owo::plugin {
namespace {

constexpr std::string_view kRecordName = "install.record";

bool sha256_text(const std::string_view value) {
    return value.size() == 64 && std::all_of(value.begin(), value.end(), [](const unsigned char byte) {
        return (byte >= '0' && byte <= '9') || (byte >= 'a' && byte <= 'f');
    });
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
        !fields.contains("plugin_id") || !fields.contains("version") ||
        !sha256_text(fields["inventory_sha256"]) ||
        !sha256_text(fields["publisher_certificate_sha256"])) return false;
    result = {fields["plugin_id"], fields["version"], fields["inventory_sha256"],
              fields["publisher_certificate_sha256"]};
    return true;
}

PluginStoreResult failure(std::string diagnostic) {
    return {false, {}, {}, {}, std::move(diagnostic)};
}

}  // namespace

PluginStoreResult initialize_plugin_store(const std::filesystem::path& root) {
#ifdef _WIN32
    if (!root.is_absolute() || root == root.root_path() || !safe_local_parent(root.parent_path()))
        return failure("plugin store must be a local absolute child of a safe existing parent");
    if (!ensure_directory(root)) return failure(windows_error("cannot create safe plugin store root"));
    for (const auto* child : {L"versions", L"records", L"active", L"data", L"staging"}) {
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
    const auto manifest = load_manifest(normalized_staging / L"manifest.json");
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
    const auto installed = root / L"versions" / std::filesystem::path(plugin_id) /
                           std::filesystem::path(version);
    const auto manifest = load_manifest(installed / L"manifest.json");
    if (!manifest.ok || manifest.value.id != plugin_id || manifest.value.version != version ||
        !safe_directory(installed)) return failure("requested installed plugin version is invalid");
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

}  // namespace owo::plugin
