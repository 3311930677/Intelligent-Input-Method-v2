#include "owo/plugin/package_archive.h"

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace {

struct Entry {
    std::string path;
    std::string local_path;
    std::string data;
    std::uint16_t flags{0x0800U};
    std::uint16_t method{};
    std::uint32_t crc32{};
    std::uint32_t declared_uncompressed{};
    std::uint16_t version_made_by{20U};
    std::uint32_t external_attributes{};
};

void put16(std::vector<unsigned char>& out, const std::uint16_t value) {
    out.push_back(static_cast<unsigned char>(value));
    out.push_back(static_cast<unsigned char>(value >> 8U));
}

void put32(std::vector<unsigned char>& out, const std::uint32_t value) {
    put16(out, static_cast<std::uint16_t>(value));
    put16(out, static_cast<std::uint16_t>(value >> 16U));
}

void text(std::vector<unsigned char>& out, const std::string& value) {
    out.insert(out.end(), value.begin(), value.end());
}

std::vector<unsigned char> package(const std::vector<Entry>& entries) {
    std::vector<unsigned char> output;
    std::vector<std::uint32_t> offsets;
    for (const auto& entry : entries) {
        offsets.push_back(static_cast<std::uint32_t>(output.size()));
        const auto& local_name = entry.local_path.empty() ? entry.path : entry.local_path;
        put32(output, 0x04034b50U); put16(output, 20); put16(output, entry.flags);
        put16(output, entry.method); put16(output, 0); put16(output, 0); put32(output, entry.crc32);
        put32(output, static_cast<std::uint32_t>(entry.data.size()));
        put32(output, entry.declared_uncompressed == 0 ? static_cast<std::uint32_t>(entry.data.size())
                                                       : entry.declared_uncompressed);
        put16(output, static_cast<std::uint16_t>(local_name.size())); put16(output, 0);
        text(output, local_name); text(output, entry.data);
    }
    const auto central_offset = static_cast<std::uint32_t>(output.size());
    for (std::size_t index = 0; index < entries.size(); ++index) {
        const auto& entry = entries[index];
        put32(output, 0x02014b50U); put16(output, entry.version_made_by); put16(output, 20);
        put16(output, entry.flags); put16(output, entry.method); put16(output, 0); put16(output, 0);
        put32(output, entry.crc32); put32(output, static_cast<std::uint32_t>(entry.data.size()));
        put32(output, entry.declared_uncompressed == 0 ? static_cast<std::uint32_t>(entry.data.size())
                                                       : entry.declared_uncompressed);
        put16(output, static_cast<std::uint16_t>(entry.path.size())); put16(output, 0); put16(output, 0);
        put16(output, 0); put16(output, 0); put32(output, entry.external_attributes); put32(output, offsets[index]);
        text(output, entry.path);
    }
    const auto central_size = static_cast<std::uint32_t>(output.size()) - central_offset;
    put32(output, 0x06054b50U); put16(output, 0); put16(output, 0);
    put16(output, static_cast<std::uint16_t>(entries.size()));
    put16(output, static_cast<std::uint16_t>(entries.size()));
    put32(output, central_size); put32(output, central_offset); put16(output, 0);
    return output;
}

bool inspect(const std::filesystem::path& path, const std::vector<Entry>& entries) {
    const auto bytes = package(entries);
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    output.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    output.close();
    return owo::plugin::inspect_package(path).ok;
}

owo::plugin::PackageInspection inspection(const std::filesystem::path& path,
                                          const std::vector<Entry>& entries) {
    const auto bytes = package(entries);
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    output.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    output.close();
    return owo::plugin::inspect_package(path);
}

}  // namespace

int main(const int argc, char** argv) {
    if (argc != 2) return 1;
    const std::filesystem::path path(argv[1]);
    const std::vector<Entry> valid{{"manifest.json", {}, "{}"}, {"bin/example.exe", {}, "MZ"}};
    const auto baseline = inspection(path, valid);
    if (!baseline.ok || baseline.inventory_sha256.size() != 64 ||
        baseline.entries.size() != 2 || baseline.entries[1].compressed_sha256.size() != 64) return 2;
    if (inspect(path, {{"../manifest.json", {}, "{}"}})) return 3;
    if (inspect(path, {{"manifest.json", {}, "{}"}, {"MANIFEST.JSON", {}, "{}"}})) return 4;
    if (inspect(path, {{"bin/example.exe", {}, "MZ"}})) return 5;
    auto encrypted = valid; encrypted[1].flags |= 1U;
    if (inspect(path, encrypted)) return 6;
    auto symlink = valid; symlink[1].version_made_by = static_cast<std::uint16_t>((3U << 8U) | 20U);
    symlink[1].external_attributes = static_cast<std::uint32_t>(0120000U) << 16U;
    if (inspect(path, symlink)) return 7;
    auto mismatch = valid; mismatch[1].local_path = "bin/other.exe";
    if (inspect(path, mismatch)) return 8;
    auto bomb = valid; bomb[1].method = 8; bomb[1].data = "x"; bomb[1].declared_uncompressed = 32U * 1024U * 1024U;
    if (inspect(path, bomb)) return 9;
    if (inspect(path, {{"manifest.json", {}, "{}"}, {"bin/CON.exe", {}, "x"}})) return 10;
    const std::vector<Entry> reordered(valid.rbegin(), valid.rend());
    if (inspection(path, reordered).inventory_sha256 != baseline.inventory_sha256) return 11;
    auto changed = valid; changed[1].data = "NZ";
    if (inspection(path, changed).inventory_sha256 == baseline.inventory_sha256) return 12;
    auto crc_changed = valid; crc_changed[1].crc32 = 1;
    if (inspection(path, crc_changed).inventory_sha256 == baseline.inventory_sha256) return 13;
    auto signed_one = valid; signed_one.push_back({"signature.json", {}, "first"});
    const auto signed_digest = inspection(path, signed_one).inventory_sha256;
    signed_one.back().data = "second";
    if (signed_digest != baseline.inventory_sha256 ||
        inspection(path, signed_one).inventory_sha256 != baseline.inventory_sha256) return 14;
    std::error_code error;
    std::filesystem::remove(path, error);
    return 0;
}
