#include "owo/plugin/package_archive.h"
#include "owo/plugin/package_signature.h"
#include "owo/plugin/plugin_installer.h"

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace {

struct Entry {
    std::string path;
    std::string data;
};

void put16(std::vector<unsigned char>& output, const std::uint16_t value) {
    output.push_back(static_cast<unsigned char>(value));
    output.push_back(static_cast<unsigned char>(value >> 8U));
}

void put32(std::vector<unsigned char>& output, const std::uint32_t value) {
    put16(output, static_cast<std::uint16_t>(value));
    put16(output, static_cast<std::uint16_t>(value >> 16U));
}

std::vector<unsigned char> package(const std::vector<Entry>& entries) {
    std::vector<unsigned char> output;
    std::vector<std::uint32_t> offsets;
    for (const auto& entry : entries) {
        offsets.push_back(static_cast<std::uint32_t>(output.size()));
        put32(output, 0x04034b50U); put16(output, 20); put16(output, 0x0800U);
        put16(output, 0); put16(output, 0); put16(output, 0); put32(output, 0);
        put32(output, static_cast<std::uint32_t>(entry.data.size()));
        put32(output, static_cast<std::uint32_t>(entry.data.size()));
        put16(output, static_cast<std::uint16_t>(entry.path.size())); put16(output, 0);
        output.insert(output.end(), entry.path.begin(), entry.path.end());
        output.insert(output.end(), entry.data.begin(), entry.data.end());
    }
    const auto central_offset = static_cast<std::uint32_t>(output.size());
    for (std::size_t index = 0; index < entries.size(); ++index) {
        const auto& entry = entries[index];
        put32(output, 0x02014b50U); put16(output, 20); put16(output, 20);
        put16(output, 0x0800U); put16(output, 0); put16(output, 0); put16(output, 0);
        put32(output, 0); put32(output, static_cast<std::uint32_t>(entry.data.size()));
        put32(output, static_cast<std::uint32_t>(entry.data.size()));
        put16(output, static_cast<std::uint16_t>(entry.path.size()));
        put16(output, 0); put16(output, 0); put16(output, 0); put16(output, 0);
        put32(output, 0); put32(output, offsets[index]);
        output.insert(output.end(), entry.path.begin(), entry.path.end());
    }
    const auto central_size = static_cast<std::uint32_t>(output.size()) - central_offset;
    put32(output, 0x06054b50U); put16(output, 0); put16(output, 0);
    put16(output, static_cast<std::uint16_t>(entries.size()));
    put16(output, static_cast<std::uint16_t>(entries.size()));
    put32(output, central_size); put32(output, central_offset); put16(output, 0);
    return output;
}

bool write_package(const std::filesystem::path& path, const std::vector<Entry>& entries) {
    const auto bytes = package(entries);
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    output.write(reinterpret_cast<const char*>(bytes.data()),
                 static_cast<std::streamsize>(bytes.size()));
    return static_cast<bool>(output);
}

}  // namespace

int main(const int argc, char** argv) {
    if (argc != 3) return 1;
    const std::filesystem::path package_path(argv[1]);
    const std::filesystem::path store_root(argv[2]);
    std::error_code error;
    std::filesystem::remove_all(store_root, error);
    std::filesystem::remove(package_path, error);

    const auto missing = owo::plugin::install_plugin_package(package_path, store_root);
    if (missing.ok || missing.stage != owo::plugin::PluginInstallStage::package_inspection ||
        std::filesystem::exists(store_root)) return 2;

    const std::string manifest =
        "{\"id\":\"owo.plugin.transaction\",\"name\":\"Transaction\","
        "\"version\":\"1.0.0\",\"api_version\":1,\"runtime\":\"process\","
        "\"entry\":\"bin/plugin.exe\",\"permissions\":[],\"network\":false,"
        "\"config_schema\":\"config.schema.json\"}";
    std::vector<Entry> entries{{"manifest.json", manifest}, {"bin/plugin.exe", "MZ"},
                               {"config.schema.json", "{}"}};
    if (!write_package(package_path, entries)) return 3;
    const auto unsigned_snapshot = owo::plugin::inspect_package(package_path);
    if (!unsigned_snapshot.ok || unsigned_snapshot.inventory_sha256.size() != 64) return 4;
    entries.push_back({"signature.json",
        "{\"schema_version\":1,\"inventory_sha256\":\"" +
        unsigned_snapshot.inventory_sha256 +
        "\",\"format\":\"cms-detached-sha256\",\"signature_base64\":\"MAMCAQE=\"}"});
    if (!write_package(package_path, entries)) return 5;

    const auto captured = owo::plugin::inspect_package(package_path);
    if (!captured.ok) return 6;
    if (!write_package(package_path, {{"manifest.json", "{}"}})) return 7;
    if (!owo::plugin::inspect_signed_package_metadata(captured).ok) return 8;

    if (!write_package(package_path, entries)) return 9;
    const auto untrusted = owo::plugin::install_plugin_package(package_path, store_root);
    if (untrusted.ok || untrusted.stage != owo::plugin::PluginInstallStage::publisher_trust ||
        untrusted.version_published || untrusted.activated ||
        std::filesystem::exists(store_root)) return 10;

    std::filesystem::remove(package_path, error);
    if (error) return 11;
    return 0;
}
