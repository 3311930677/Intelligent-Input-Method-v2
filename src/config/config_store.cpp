#include "owo/config/config_store.h"

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#endif

#include <charconv>
#include <algorithm>
#include <fstream>
#include <limits>
#include <map>
#include <system_error>

namespace owo::config {
namespace {

constexpr std::uintmax_t kMaximumConfigBytes = 16U * 1024U;

bool parse_u32(const std::string_view text, std::uint32_t& output) {
    const auto result = std::from_chars(text.data(), text.data() + text.size(), output);
    return result.ec == std::errc{} && result.ptr == text.data() + text.size();
}

bool parse_bool(const std::string_view text, bool& output) {
    if (text == "true") {
        output = true;
        return true;
    }
    if (text == "false") {
        output = false;
        return true;
    }
    return false;
}

ConfigParseResult read_config(const std::filesystem::path& path) {
    std::error_code error;
    const auto size = std::filesystem::file_size(path, error);
    if (error) return {false, {}, "configuration file is missing"};
    if (size == 0 || size > kMaximumConfigBytes)
        return {false, {}, "configuration file is empty or too large"};
    std::ifstream input(path, std::ios::binary);
    if (!input) return {false, {}, "cannot open configuration file"};
    std::string bytes(static_cast<std::size_t>(size), '\0');
    if (!input.read(bytes.data(), static_cast<std::streamsize>(bytes.size())))
        return {false, {}, "cannot read configuration file"};
    return parse_config(bytes);
}

bool write_durable(const std::filesystem::path& path, const std::string_view bytes) {
#ifdef _WIN32
    const HANDLE file = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                                    FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return false;
    std::size_t offset = 0;
    bool ok = true;
    while (offset < bytes.size()) {
        const auto remaining = bytes.size() - offset;
        const auto chunk = static_cast<DWORD>((std::min)(remaining,
            static_cast<std::size_t>((std::numeric_limits<DWORD>::max)())));
        DWORD written{};
        if (!WriteFile(file, bytes.data() + offset, chunk, &written, nullptr) || written != chunk) {
            ok = false;
            break;
        }
        offset += written;
    }
    if (ok && !FlushFileBuffers(file)) ok = false;
    CloseHandle(file);
    return ok;
#else
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    output.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    output.flush();
    return static_cast<bool>(output);
#endif
}

bool replace_file(const std::filesystem::path& temporary, const std::filesystem::path& target) {
#ifdef _WIN32
    return MoveFileExW(temporary.c_str(), target.c_str(),
                       MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != FALSE;
#else
    std::error_code error;
    std::filesystem::rename(temporary, target, error);
    return !error;
#endif
}

}  // namespace

ConfigValidationResult validate_config(const AppConfig& value) {
    if (value.candidate_page_size < 1 || value.candidate_page_size > 9)
        return {false, "candidate_page_size must be between 1 and 9"};
    if (value.model_timeout_ms < 5 || value.model_timeout_ms > 500)
        return {false, "model_timeout_ms must be between 5 and 500"};
    return {true, {}};
}

ConfigParseResult parse_config(const std::string_view utf8) {
    if (utf8.empty() || utf8.size() > kMaximumConfigBytes)
        return {false, {}, "configuration is empty or too large"};
    if (utf8.size() >= 3 && static_cast<unsigned char>(utf8[0]) == 0xef &&
        static_cast<unsigned char>(utf8[1]) == 0xbb &&
        static_cast<unsigned char>(utf8[2]) == 0xbf)
        return {false, {}, "UTF-8 BOM is forbidden"};
    std::map<std::string, std::string> fields;
    std::size_t offset = 0;
    while (offset < utf8.size()) {
        const auto end = utf8.find('\n', offset);
        auto line = utf8.substr(offset, end == std::string_view::npos ? utf8.size() - offset
                                                                      : end - offset);
        if (!line.empty() && line.back() == '\r') line.remove_suffix(1);
        if (line.empty()) return {false, {}, "blank configuration lines are forbidden"};
        const auto separator = line.find('=');
        if (separator == std::string_view::npos || separator == 0 || separator + 1 == line.size())
            return {false, {}, "invalid configuration field"};
        if (!fields.emplace(std::string(line.substr(0, separator)),
                            std::string(line.substr(separator + 1))).second)
            return {false, {}, "duplicate configuration field"};
        if (end == std::string_view::npos) break;
        offset = end + 1;
        if (offset == utf8.size()) break;
    }
    constexpr std::string_view required[]{"schema_version", "candidate_page_size",
        "user_learning_enabled", "model_ranking_enabled", "model_timeout_ms"};
    if (fields.size() != std::size(required))
        return {false, {}, "configuration fields are missing or unknown"};
    for (const auto key : required)
        if (!fields.contains(std::string(key)))
            return {false, {}, "configuration fields are missing or unknown"};
    std::uint32_t version{};
    AppConfig value;
    if (!parse_u32(fields["schema_version"], version) || version != kConfigSchemaVersion)
        return {false, {}, "unsupported configuration schema_version"};
    if (!parse_u32(fields["candidate_page_size"], value.candidate_page_size) ||
        !parse_bool(fields["user_learning_enabled"], value.user_learning_enabled) ||
        !parse_bool(fields["model_ranking_enabled"], value.model_ranking_enabled) ||
        !parse_u32(fields["model_timeout_ms"], value.model_timeout_ms))
        return {false, {}, "configuration field type is invalid"};
    const auto validation = validate_config(value);
    if (!validation.ok) return {false, {}, validation.diagnostic};
    return {true, value, {}};
}

std::string serialize_config(const AppConfig& value) {
    if (!validate_config(value).ok) return {};
    return "schema_version=1\ncandidate_page_size=" + std::to_string(value.candidate_page_size) +
           "\nuser_learning_enabled=" + (value.user_learning_enabled ? std::string("true") : "false") +
           "\nmodel_ranking_enabled=" + (value.model_ranking_enabled ? std::string("true") : "false") +
           "\nmodel_timeout_ms=" + std::to_string(value.model_timeout_ms) + "\n";
}

ConfigIoResult ConfigStore::load(const std::filesystem::path& path) {
    path_ = path;
    const auto main = read_config(path_);
    if (main.ok) {
        const bool changed = generation_ == 0 || current_ != main.value;
        current_ = main.value;
        if (changed) ++generation_;
        return {true, false, false, changed, generation_, {}};
    }
    auto backup_path = path_;
    backup_path += L".bak";
    const auto backup = read_config(backup_path);
    if (backup.ok) {
        const bool changed = generation_ == 0 || current_ != backup.value;
        current_ = backup.value;
        if (changed) ++generation_;
        return {true, true, false, changed, generation_, "recovered configuration backup"};
    }
    const AppConfig defaults;
    const bool changed = generation_ == 0 || current_ != defaults;
    current_ = defaults;
    if (changed) ++generation_;
    const bool missing = !std::filesystem::exists(path_) && !std::filesystem::exists(backup_path);
    return {true, false, true, changed, generation_,
            missing ? "configuration missing; using defaults"
                    : "configuration and backup invalid; using defaults"};
}

ConfigIoResult ConfigStore::reload() {
    if (path_.empty()) return {false, false, false, false, generation_, "configuration path is not set"};
    const auto parsed = read_config(path_);
    if (!parsed.ok) return {false, false, false, false, generation_, parsed.diagnostic};
    const bool changed = current_ != parsed.value;
    if (changed) {
        current_ = parsed.value;
        ++generation_;
    }
    return {true, false, false, changed, generation_, {}};
}

ConfigIoResult ConfigStore::save(const AppConfig& value) {
    if (path_.empty()) return {false, false, false, false, generation_, "configuration path is not set"};
    const auto validation = validate_config(value);
    if (!validation.ok) return {false, false, false, false, generation_, validation.diagnostic};
    auto temporary = path_;
    temporary += L".tmp";
    auto backup = path_;
    backup += L".bak";
    const auto bytes = serialize_config(value);
    if (!write_durable(temporary, bytes)) {
        std::error_code ignored;
        std::filesystem::remove(temporary, ignored);
        return {false, false, false, false, generation_, "cannot durably write temporary configuration"};
    }
    const auto existing = read_config(path_);
    if (existing.ok) {
        auto backup_temporary = backup;
        backup_temporary += L".tmp";
        if (!write_durable(backup_temporary, serialize_config(existing.value)) ||
            !replace_file(backup_temporary, backup)) {
            std::error_code ignored;
            std::filesystem::remove(backup_temporary, ignored);
            std::filesystem::remove(temporary, ignored);
            return {false, false, false, false, generation_, "cannot update configuration backup"};
        }
    }
    if (!replace_file(temporary, path_)) {
        std::error_code ignored;
        std::filesystem::remove(temporary, ignored);
        return {false, false, false, false, generation_, "cannot atomically replace configuration"};
    }
    const bool changed = generation_ == 0 || current_ != value;
    current_ = value;
    if (changed) ++generation_;
    return {true, false, false, changed, generation_, {}};
}

}  // namespace owo::config
