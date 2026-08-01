#pragma once

#include <array>
#include <string_view>

namespace owo::plugin {

inline constexpr std::array<std::string_view, 5> kKnownPluginPermissions{
    "clipboard.read", "clipboard.write", "input.commit", "input.context", "input.replace"};

[[nodiscard]] inline bool is_known_plugin_permission(const std::string_view permission) noexcept {
    for (const auto known : kKnownPluginPermissions) {
        if (permission == known) return true;
    }
    return false;
}

}  // namespace owo::plugin
