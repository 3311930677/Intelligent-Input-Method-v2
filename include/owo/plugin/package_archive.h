#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace owo::plugin {

struct PackageEntry {
    std::string path;
    std::uint16_t compression_method{};
    std::uint64_t compressed_size{};
    std::uint64_t uncompressed_size{};
};

struct PackageInspection {
    bool ok{};
    std::vector<PackageEntry> entries;
    std::uint64_t total_uncompressed_size{};
    std::string diagnostic;
};

/// Preflights a bounded ZIP-compatible .owopkg without extracting or executing its contents.
[[nodiscard]] PackageInspection inspect_package(const std::filesystem::path& package_path);

}  // namespace owo::plugin
