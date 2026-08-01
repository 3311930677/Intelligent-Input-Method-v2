#pragma once

#include "owo/plugin/plugin_manifest.h"

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace owo::plugin {

inline constexpr std::uint32_t kPluginAuthorizationSchemaVersion = 1;

struct PluginAuthorization {
    std::uint32_t schema_version{kPluginAuthorizationSchemaVersion};
    std::string plugin_id;
    std::string version;
    std::string inventory_sha256;
    std::string publisher_certificate_sha256;
    std::vector<std::string> granted_permissions;
};

struct PluginAuthorizationResult {
    bool ok{};
    PluginAuthorization value;
    std::string diagnostic;
};

/// Creates a version- and publisher-bound grant. Every grant must be declared by the manifest.
[[nodiscard]] PluginAuthorizationResult make_plugin_authorization(
    const PluginManifest& manifest, std::string_view inventory_sha256,
    std::string_view publisher_certificate_sha256,
    std::vector<std::string> granted_permissions);

/// Serializes a strict canonical v1 authorization record; invalid records return empty.
[[nodiscard]] std::string serialize_plugin_authorization(const PluginAuthorization& authorization);

/// Parses the exact canonical v1 record format. Unknown, reordered, or duplicate fields fail.
[[nodiscard]] PluginAuthorizationResult parse_plugin_authorization(std::string_view record);

/// Returns true only when identity, version, inventory, publisher, and permission all match.
[[nodiscard]] bool is_plugin_permission_granted(
    const PluginAuthorization& authorization, const PluginManifest& installed_manifest,
    std::string_view inventory_sha256,
    std::string_view publisher_certificate_sha256, std::string_view permission);

}  // namespace owo::plugin
