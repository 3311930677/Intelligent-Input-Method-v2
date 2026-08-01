#pragma once

#include "owo/plugin/plugin_manifest.h"

#include <filesystem>
#include <string>

namespace owo::plugin {

struct PluginStoreResult {
    bool ok{};
    PluginManifest manifest;
    std::filesystem::path installed_path;
    std::string previous_version;
    std::string diagnostic;
    bool version_published{};
    bool activated{};
};

/// Creates or validates the versioned plugin-store layout. The data directory is never replaced.
[[nodiscard]] PluginStoreResult initialize_plugin_store(const std::filesystem::path& root);

/// Publishes one prevalidated direct child of root/staging, then atomically activates it.
[[nodiscard]] PluginStoreResult publish_staged_plugin(
    const std::filesystem::path& root, const std::filesystem::path& staging_directory,
    std::string_view inventory_sha256, std::string_view publisher_certificate_sha256);

/// Atomically switches the active record to an already installed version.
[[nodiscard]] PluginStoreResult activate_installed_plugin_version(
    const std::filesystem::path& root, std::string_view plugin_id, std::string_view version);

}  // namespace owo::plugin
