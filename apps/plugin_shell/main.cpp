#include "owo/plugin/plugin_store.h"

#include <Windows.h>

#include <charconv>
#include <iostream>
#include <string>

namespace {

std::string utf8(const std::wstring_view value) {
    if (value.empty()) return {};
    const auto size = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value.data(),
                                          static_cast<int>(value.size()), nullptr, 0,
                                          nullptr, nullptr);
    if (size <= 0) return {};
    std::string result(static_cast<std::size_t>(size), '\0');
    if (WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value.data(),
                            static_cast<int>(value.size()), result.data(), size,
                            nullptr, nullptr) != size) return {};
    return result;
}

std::string json_escape(const std::string_view value) {
    static constexpr char digits[] = "0123456789abcdef";
    std::string result;
    result.reserve(value.size() + 2);
    result.push_back('"');
    for (const unsigned char byte : value) {
        switch (byte) {
        case '"': result += "\\\""; break;
        case '\\': result += "\\\\"; break;
        case '\b': result += "\\b"; break;
        case '\f': result += "\\f"; break;
        case '\n': result += "\\n"; break;
        case '\r': result += "\\r"; break;
        case '\t': result += "\\t"; break;
        default:
            if (byte < 0x20) {
                result += "\\u00";
                result.push_back(digits[byte >> 4]);
                result.push_back(digits[byte & 0x0f]);
            } else {
                result.push_back(static_cast<char>(byte));
            }
        }
    }
    result.push_back('"');
    return result;
}

const char* recovery_kind(const owo::plugin::PluginRecoveryKind kind) {
    using enum owo::plugin::PluginRecoveryKind;
    switch (kind) {
    case retained_staging: return "retained_staging";
    case orphaned_version: return "orphaned_version";
    case orphaned_record: return "orphaned_record";
    case orphaned_authorization: return "orphaned_authorization";
    case inactive_version: return "inactive_version";
    case invalid_active_record: return "invalid_active_record";
    case unsafe_store_entry: return "unsafe_store_entry";
    }
    return "unknown";
}

const char* recovery_action(const owo::plugin::PluginRecoveryKind kind) {
    if (kind == owo::plugin::PluginRecoveryKind::inactive_version) return "activate";
    if (kind == owo::plugin::PluginRecoveryKind::unsafe_store_entry) return "manual";
    return "cleanup";
}

void print_management_result(const owo::plugin::PluginManagementResult& result) {
    std::cout << "{\"ok\":" << (result.ok ? "true" : "false")
              << ",\"plugin_id\":" << json_escape(result.plugin_id)
              << ",\"version\":" << json_escape(result.version)
              << ",\"path\":" << json_escape(utf8(result.affected_path.wstring()))
              << ",\"diagnostic\":" << json_escape(result.diagnostic) << "}\n";
}

int list(const std::filesystem::path& root) {
    const auto plugins = owo::plugin::list_installed_plugins(root);
    if (!plugins.ok) {
        std::cerr << plugins.diagnostic << '\n';
        return 2;
    }
    const auto recovery = owo::plugin::scan_plugin_store_recovery(root);
    if (!recovery.ok) {
        std::cerr << recovery.diagnostic << '\n';
        return 2;
    }
    std::cout << "{\"schema_version\":1,\"plugins\":[";
    for (std::size_t index = 0; index < plugins.versions.size(); ++index) {
        const auto& plugin = plugins.versions[index];
        if (index != 0) std::cout << ',';
        std::cout << "{\"id\":" << json_escape(plugin.manifest.id)
                  << ",\"name\":" << json_escape(plugin.manifest.name)
                  << ",\"version\":" << json_escape(plugin.manifest.version)
                  << ",\"active\":" << (plugin.active ? "true" : "false") << '}';
    }
    std::cout << "],\"recovery\":[";
    for (std::size_t index = 0; index < recovery.items.size(); ++index) {
        const auto& item = recovery.items[index];
        if (index != 0) std::cout << ',';
        std::cout << "{\"index\":" << index
                  << ",\"kind\":" << json_escape(recovery_kind(item.kind))
                  << ",\"action\":" << json_escape(recovery_action(item.kind))
                  << ",\"path\":" << json_escape(utf8(item.path.wstring()))
                  << ",\"plugin_id\":" << json_escape(item.plugin_id)
                  << ",\"version\":" << json_escape(item.version)
                  << ",\"diagnostic\":" << json_escape(item.diagnostic) << '}';
    }
    std::cout << "]}\n";
    return 0;
}

}  // namespace

int wmain(const int argc, wchar_t** argv) {
    if (argc < 3) {
        std::cerr << "usage: owo_plugin_shell <store-root> <list|activate|deactivate|cleanup> ...\n";
        return 1;
    }
    SetConsoleOutputCP(CP_UTF8);
    const std::filesystem::path root(argv[1]);
    const std::wstring_view command(argv[2]);
    if (command == L"list" && argc == 3) return list(root);
    if (command == L"activate" && argc == 5) {
        const auto result = owo::plugin::activate_installed_plugin_version(
            root, utf8(argv[3]), utf8(argv[4]));
        if (!result.ok) {
            std::cerr << result.diagnostic << '\n';
            return 2;
        }
        print_management_result({true, result.manifest.id, result.manifest.version,
                                 result.installed_path, {}});
        return 0;
    }
    if (command == L"deactivate" && argc == 5) {
        const auto result = owo::plugin::deactivate_plugin(
            root, utf8(argv[3]), utf8(argv[4]));
        if (!result.ok) {
            std::cerr << result.diagnostic << '\n';
            return 2;
        }
        print_management_result(result);
        return 0;
    }
    if (command == L"cleanup" && argc == 8) {
        std::size_t index = 0;
        const auto text = utf8(argv[3]);
        const auto parsed = std::from_chars(text.data(), text.data() + text.size(), index);
        const auto scan = owo::plugin::scan_plugin_store_recovery(root);
        if (parsed.ec != std::errc{} || parsed.ptr != text.data() + text.size() ||
            !scan.ok || index >= scan.items.size()) {
            std::cerr << (scan.ok ? "recovery index is invalid" : scan.diagnostic) << '\n';
            return 2;
        }
        const auto& selected = scan.items[index];
        if (utf8(argv[4]) != recovery_kind(selected.kind) ||
            std::filesystem::path(argv[5]).lexically_normal() != selected.path ||
            utf8(argv[6]) != selected.plugin_id || utf8(argv[7]) != selected.version) {
            std::cerr << "recovery selection changed; refresh before applying cleanup\n";
            return 2;
        }
        const auto result = owo::plugin::cleanup_plugin_recovery_item(root, selected);
        if (!result.ok) {
            std::cerr << result.diagnostic << '\n';
            return 2;
        }
        print_management_result(result);
        return 0;
    }
    std::cerr << "invalid plugin management command or argument count\n";
    return 1;
}
