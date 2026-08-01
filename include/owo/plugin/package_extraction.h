#pragma once

#include "owo/plugin/package_archive.h"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>

namespace owo::plugin {

struct ExtractionResult {
    bool ok{};
    std::size_t files_written{};
    std::uint64_t bytes_written{};
    std::string diagnostic;
};

[[nodiscard]] bool deflate_extraction_available() noexcept;

/// Extracts Store entries from an immutable preflight snapshot into a new staging directory.
/// This is not an install operation and does not establish package trust.
[[nodiscard]] ExtractionResult extract_package_to_staging(
    const PackageInspection& package, const std::filesystem::path& staging_directory);

}  // namespace owo::plugin
