#include "owo/plugin/plugin_authorization_store.h"
#include "owo/plugin/plugin_store.h"

#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>

namespace {

std::string manifest_json(const std::string& version) {
    return "{\"id\":\"owo.plugin.example\",\"name\":\"Example\",\"version\":\"" + version +
           "\",\"api_version\":1,\"runtime\":\"process\",\"entry\":\"bin/example.exe\","
           "\"permissions\":[\"input.context\"],\"network\":false,"
           "\"config_schema\":\"config.schema.json\"}";
}

bool write_file(const std::filesystem::path& path, const std::string& bytes) {
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    output.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    return static_cast<bool>(output);
}

bool create_staging(const std::filesystem::path& root, const std::string& name,
                    const std::string& version) {
    const auto staging = root / "staging" / name;
    std::error_code error;
    std::filesystem::create_directories(staging / "bin", error);
    return !error && write_file(staging / "manifest.json", manifest_json(version)) &&
           write_file(staging / "config.schema.json", "{}") &&
           write_file(staging / "bin" / "example.exe", "MZ");
}

std::string read_file(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}

bool has_item(const owo::plugin::PluginRecoveryScanResult& scan,
              const owo::plugin::PluginRecoveryKind kind,
              const std::string& version = {}) {
    for (const auto& item : scan.items) {
        if (item.kind == kind && (version.empty() || item.version == version)) return true;
    }
    return false;
}

}  // namespace

int main(const int argc, char** argv) {
    if (argc != 2) return 1;
    const std::filesystem::path root(argv[1]);
    std::error_code error;
    std::filesystem::remove_all(root, error);
    if (error) return 2;
    const auto empty_scan = owo::plugin::scan_plugin_store_recovery(root);
    if (!empty_scan.ok || !empty_scan.items.empty() || std::filesystem::exists(root)) return 15;
    if (owo::plugin::scan_plugin_store_recovery("relative-plugin-store").ok) return 23;
    const auto initialized = owo::plugin::initialize_plugin_store(root);
    if (!initialized.ok || !std::filesystem::is_directory(root / "versions") ||
        !std::filesystem::is_directory(root / "authorizations") ||
        !std::filesystem::is_directory(root / "data") ||
        !std::filesystem::is_directory(root / "staging")) return 3;
    if (!create_staging(root, ".stage-v1", "1.0.0")) return 4;
    const auto first = owo::plugin::publish_staged_plugin(
        root, root / "staging" / ".stage-v1", std::string(64, 'a'), std::string(64, 'b'));
    if (!first.ok || !first.version_published || !first.activated ||
        !first.previous_version.empty() ||
        first.manifest.version != "1.0.0" || !std::filesystem::is_directory(first.installed_path) ||
        std::filesystem::exists(root / "staging" / ".stage-v1")) return 5;
    const auto binding = owo::plugin::query_installed_plugin_version(
        root, "owo.plugin.example", "1.0.0");
    if (!binding.ok || binding.inventory_sha256 != std::string(64, 'a') ||
        binding.publisher_certificate_sha256 != std::string(64, 'b')) return 24;
    const auto active_binding = owo::plugin::query_active_plugin_version(
        root, "owo.plugin.example");
    if (!active_binding.ok || active_binding.manifest.version != "1.0.0" ||
        active_binding.installed_path != binding.installed_path ||
        active_binding.inventory_sha256 != binding.inventory_sha256 ||
        active_binding.publisher_certificate_sha256 !=
            binding.publisher_certificate_sha256) return 31;
    const auto authorization = owo::plugin::make_plugin_authorization(
        first.manifest, binding.inventory_sha256, binding.publisher_certificate_sha256,
        {"input.context"});
    if (!authorization.ok) return 25;
    const auto saved_authorization = owo::plugin::save_plugin_authorization(root, authorization.value);
    const auto loaded_authorization = owo::plugin::load_plugin_authorization(
        root, "owo.plugin.example", "1.0.0");
    if (!saved_authorization.ok || !loaded_authorization.ok ||
        !owo::plugin::is_plugin_permission_granted(
            loaded_authorization.value, first.manifest, binding.inventory_sha256,
            binding.publisher_certificate_sha256, "input.context")) return 26;
    auto forged_authorization = authorization.value;
    forged_authorization.inventory_sha256.assign(64, 'f');
    if (owo::plugin::save_plugin_authorization(root, forged_authorization).ok) return 27;
    forged_authorization = authorization.value;
    forged_authorization.schema_version = 2;
    if (owo::plugin::save_plugin_authorization(root, forged_authorization).ok) return 30;
    const auto active = root / "active" / "owo.plugin.example.record";
    if (read_file(active).find("version=1.0.0\n") == std::string::npos) return 6;
    const auto data_marker = root / "data" / "owo.plugin.example" / "marker.txt";
    if (!write_file(data_marker, "persistent")) return 7;

    if (!create_staging(root, ".stage-v2", "2.0.0")) return 8;
    const auto second = owo::plugin::publish_staged_plugin(
        root, root / "staging" / ".stage-v2", std::string(64, 'c'), std::string(64, 'd'));
    if (!second.ok || !second.version_published || !second.activated ||
        second.previous_version != "1.0.0" ||
        !std::filesystem::is_directory(root / "versions" / "owo.plugin.example" / "1.0.0") ||
        !std::filesystem::is_directory(root / "versions" / "owo.plugin.example" / "2.0.0") ||
        read_file(data_marker) != "persistent") return 9;
    if (owo::plugin::load_plugin_authorization(
            root, "owo.plugin.example", "2.0.0").ok) return 28;
    const auto rollback = owo::plugin::activate_installed_plugin_version(
        root, "owo.plugin.example", "1.0.0");
    if (!rollback.ok || !rollback.version_published || !rollback.activated ||
        rollback.previous_version != "2.0.0" ||
        read_file(active).find("version=1.0.0\n") == std::string::npos) return 10;

    if (!create_staging(root, ".stage-duplicate", "1.0.0")) return 11;
    if (owo::plugin::publish_staged_plugin(
            root, root / "staging" / ".stage-duplicate",
            std::string(64, 'e'), std::string(64, 'f')).ok) return 12;
    if (owo::plugin::activate_installed_plugin_version(
            root, "../escape", "1.0.0").ok) return 13;
    std::filesystem::remove_all(root / "staging" / ".stage-duplicate", error);
    if (error) return 16;
    const auto retained = root / "staging" / ".install-recovery";
    std::filesystem::create_directories(retained, error);
    if (error || !write_file(retained / "partial.bin", "partial")) return 17;
    const auto orphaned = root / "versions" / "owo.plugin.example" / "3.0.0";
    std::filesystem::create_directories(orphaned / "bin", error);
    if (error || !write_file(orphaned / "manifest.json", manifest_json("3.0.0")) ||
        !write_file(orphaned / "config.schema.json", "{}") ||
        !write_file(orphaned / "bin" / "example.exe", "MZ")) return 18;
    if (!write_file(root / "records" / "owo.plugin.example" / "9.0.0.record", "invalid\n"))
        return 19;
    const auto recovery = owo::plugin::scan_plugin_store_recovery(root);
    if (!recovery.ok || recovery.items.size() != 4 ||
        !has_item(recovery, owo::plugin::PluginRecoveryKind::retained_staging) ||
        !has_item(recovery, owo::plugin::PluginRecoveryKind::orphaned_version, "3.0.0") ||
        !has_item(recovery, owo::plugin::PluginRecoveryKind::orphaned_record) ||
        !has_item(recovery, owo::plugin::PluginRecoveryKind::inactive_version, "2.0.0") ||
        !std::filesystem::exists(retained) || !std::filesystem::exists(orphaned)) return 20;
    if (!write_file(active, "invalid\n")) return 21;
    if (!write_file(saved_authorization.record_path, "invalid\n")) return 29;
    const auto damaged = owo::plugin::scan_plugin_store_recovery(root);
    if (!damaged.ok ||
        !has_item(damaged, owo::plugin::PluginRecoveryKind::invalid_active_record) ||
        !has_item(damaged, owo::plugin::PluginRecoveryKind::orphaned_authorization) ||
        !has_item(damaged, owo::plugin::PluginRecoveryKind::inactive_version, "1.0.0")) return 22;
    std::filesystem::remove_all(root, error);
    if (error || std::filesystem::exists(root)) return 14;
    return 0;
}
