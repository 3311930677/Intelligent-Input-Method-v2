#pragma once

#include "owo/plugin/plugin_manifest.h"

#include <filesystem>
#include <string>
#include <vector>

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

enum class PluginRecoveryKind {
    retained_staging,
    orphaned_version,
    orphaned_record,
    inactive_version,
    invalid_active_record,
    unsafe_store_entry,
};

struct PluginRecoveryItem {
    PluginRecoveryKind kind{};
    std::filesystem::path path;
    std::string plugin_id;
    std::string version;
    std::string diagnostic;
};

struct PluginRecoveryScanResult {
    bool ok{};
    std::vector<PluginRecoveryItem> items;
    std::string diagnostic;
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

/// Audits recoverable startup state without deleting, activating, or otherwise mutating it.
/// A missing store root is a valid empty state; an existing unsafe or incomplete layout fails.
[[nodiscard]] PluginRecoveryScanResult scan_plugin_store_recovery(
    const std::filesystem::path& root);

}  // namespace owo::plugin
