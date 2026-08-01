#pragma once

#include "owo/plugin/plugin_manifest.h"

#include <filesystem>
#include <string>

namespace owo::plugin {

enum class PluginInstallStage {
    none,
    package_inspection,
    publisher_trust,
    store_initialization,
    staging_extraction,
    version_publication,
    completed,
};

struct PluginInstallResult {
    bool ok{};
    PluginInstallStage stage{PluginInstallStage::none};
    bool version_published{};
    bool activated{};
    PluginManifest manifest;
    std::filesystem::path installed_path;
    std::filesystem::path retained_staging_path;
    std::string previous_version;
    std::string inventory_sha256;
    std::string publisher_display_name;
    std::string publisher_certificate_sha256;
    std::string diagnostic;
};

/// Installs a package through one immutable snapshot, Windows publisher trust, safe staging,
/// and versioned atomic publication. Untrusted packages never create store state.
[[nodiscard]] PluginInstallResult install_plugin_package(
    const std::filesystem::path& package_path, const std::filesystem::path& plugin_store_root);

}  // namespace owo::plugin
