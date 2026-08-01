#include "owo/plugin/plugin_store.h"

#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>

namespace {

std::string manifest_json(const std::string& version) {
    return "{\"id\":\"owo.plugin.example\",\"name\":\"Example\",\"version\":\"" + version +
           "\",\"api_version\":1,\"runtime\":\"process\",\"entry\":\"bin/example.exe\","
           "\"permissions\":[],\"network\":false,\"config_schema\":\"config.schema.json\"}";
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

}  // namespace

int main(const int argc, char** argv) {
    if (argc != 2) return 1;
    const std::filesystem::path root(argv[1]);
    std::error_code error;
    std::filesystem::remove_all(root, error);
    if (error) return 2;
    const auto initialized = owo::plugin::initialize_plugin_store(root);
    if (!initialized.ok || !std::filesystem::is_directory(root / "versions") ||
        !std::filesystem::is_directory(root / "data") ||
        !std::filesystem::is_directory(root / "staging")) return 3;
    if (!create_staging(root, ".stage-v1", "1.0.0")) return 4;
    const auto first = owo::plugin::publish_staged_plugin(
        root, root / "staging" / ".stage-v1", std::string(64, 'a'), std::string(64, 'b'));
    if (!first.ok || !first.version_published || !first.activated ||
        !first.previous_version.empty() ||
        first.manifest.version != "1.0.0" || !std::filesystem::is_directory(first.installed_path) ||
        std::filesystem::exists(root / "staging" / ".stage-v1")) return 5;
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
    std::filesystem::remove_all(root, error);
    if (error || std::filesystem::exists(root)) return 14;
    return 0;
}
