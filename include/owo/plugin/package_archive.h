#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace owo::plugin {

struct PackageEntry {
    std::string path;
    std::uint16_t compression_method{};
    std::uint32_t crc32{};
    std::uint64_t compressed_size{};
    std::uint64_t uncompressed_size{};
    std::string compressed_sha256;
};

struct PackageInspection {
    bool ok{};
    std::vector<PackageEntry> entries;
    std::uint64_t total_uncompressed_size{};
    /// SHA-256 of the canonical inventory excluding signature.json.
    std::string inventory_sha256;
    std::string diagnostic;
};

/// Preflights a bounded ZIP-compatible .owopkg without extracting or executing its contents.
[[nodiscard]] PackageInspection inspect_package(const std::filesystem::path& package_path);

}  // namespace owo::plugin
