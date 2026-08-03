// owo_emoji_plugin: a tier-L1 (local, no network) process plugin.
//
// It answers the versioned service "emoji.query.v1": given a lowercase ASCII keyword
// payload, it returns newline-separated emoji candidates. All data is embedded, so the
// plugin needs no network, no devices, and no cross-process access; it runs unchanged in
// the zero-capability AppContainer sandbox.
//
// The handshake, cancellation and lifecycle skeleton mirrors the sample process plugin so
// the existing PluginHost contract (hello/invoke/cancel/shutdown) is honored exactly.

#include "owo/plugin/plugin_pipe.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <filesystem>
#include <mutex>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

struct Arguments {
    std::wstring pipe_name;
    std::string plugin_id;
    std::filesystem::path data_path;
};

struct EmojiEntry {
    std::string_view keyword;
    std::string_view emoji;  // UTF-8 encoded.
};

// An embedded keyword -> emoji table. Multiple emoji may share a keyword and each keyword
// may map to several candidates via repeated rows. Rows are grouped by keyword so a prefix
// match walks the whole group before moving on, which keeps candidate ranking stable.
constexpr std::array<EmojiEntry, 294> kEmojiTable{{
    // Faces: positive.
    {"smile", "\xF0\x9F\x98\x80"},       {"smile", "\xF0\x9F\x98\x8A"},
    {"smile", "\xF0\x9F\x99\x82"},       {"smile", "\xF0\x9F\x98\x83"},
    {"grin", "\xF0\x9F\x98\x81"},        {"grin", "\xF0\x9F\x98\x84"},
    {"laugh", "\xF0\x9F\x98\x82"},       {"laugh", "\xF0\x9F\xA4\xA3"},
    {"laugh", "\xF0\x9F\x98\x86"},       {"lol", "\xF0\x9F\xA4\xA3"},
    {"happy", "\xF0\x9F\x98\x8A"},       {"happy", "\xF0\x9F\x98\x84"},
    {"happy", "\xF0\x9F\xA5\xB3"},       {"joy", "\xF0\x9F\x98\x82"},
    {"wink", "\xF0\x9F\x98\x89"},        {"cool", "\xF0\x9F\x98\x8E"},
    {"cute", "\xF0\x9F\xA5\xB0"},        {"cute", "\xF0\x9F\x98\x8B"},
    {"kiss", "\xF0\x9F\x98\x98"},        {"kiss", "\xF0\x9F\x98\x9A"},
    {"blush", "\xF0\x9F\x98\x8A"},       {"blush", "\xF0\x9F\x98\x8D"},
    {"proud", "\xF0\x9F\x98\x8F"},       {"relief", "\xF0\x9F\x98\x8C"},
    {"hug", "\xF0\x9F\xA4\x97"},         {"silly", "\xF0\x9F\x98\x9C"},
    {"silly", "\xF0\x9F\x98\x9B"},       {"nerd", "\xF0\x9F\xA4\x93"},
    {"amaze", "\xF0\x9F\xA4\xA9"},       {"star", "\xE2\xAD\x90"},
    {"party", "\xF0\x9F\xA5\xB3"},       {"party", "\xF0\x9F\x8E\x89"},
    // Faces: negative and neutral.
    {"cry", "\xF0\x9F\x98\xAD"},         {"cry", "\xF0\x9F\x98\xA2"},
    {"sad", "\xF0\x9F\x98\x9E"},         {"sad", "\xF0\x9F\x98\x9F"},
    {"sad", "\xF0\x9F\x98\xA5"},         {"tear", "\xF0\x9F\xA5\xB9"},
    {"angry", "\xF0\x9F\x98\xA1"},       {"angry", "\xF0\x9F\x98\xA0"},
    {"angry", "\xF0\x9F\xA4\xAC"},       {"rage", "\xF0\x9F\x98\xA4"},
    {"fear", "\xF0\x9F\x98\xB1"},        {"fear", "\xF0\x9F\x98\xB0"},
    {"shock", "\xF0\x9F\x98\xAE"},       {"shock", "\xF0\x9F\x98\xB2"},
    {"tired", "\xF0\x9F\x98\xAA"},{"tired", "\xF0\x9F\xA5\xB4"},
    {"sleep", "\xF0\x9F\x98\xB4"},       {"sleep", "\xF0\x9F\x92\xA4"},
    {"sick", "\xF0\x9F\xA4\xA2"},        {"sick", "\xF0\x9F\xA4\x92"},
    {"mask", "\xF0\x9F\x98\xB7"},        {"dizzy", "\xF0\x9F\x98\xB5"},
    {"think", "\xF0\x9F\xA4\x94"},       {"quiet", "\xF0\x9F\xA4\xAB"},
    {"blank", "\xF0\x9F\x98\x91"},       {"blank", "\xF0\x9F\x98\x90"},
    {"roll", "\xF0\x9F\x99\x84"},        {"sweat", "\xF0\x9F\x98\x85"},
    {"sweat", "\xF0\x9F\x98\x93"},       {"shy", "\xF0\x9F\x98\xB3"},
    {"pray", "\xF0\x9F\x99\x8F"},        {"sorry", "\xF0\x9F\x99\x87"},
    {"shrug", "\xF0\x9F\xA4\xB7"},       {"salute", "\xF0\x9F\xAB\xA1"},
    // Hearts and love.
    {"love", "\xE2\x9D\xA4"},            {"love", "\xF0\x9F\x98\x8D"},
    {"love", "\xF0\x9F\x92\x96"},        {"heart", "\xE2\x9D\xA4"},
    {"heart", "\xF0\x9F\x92\x95"},       {"heart", "\xF0\x9F\x92\x99"},
    {"heart", "\xF0\x9F\x92\x9C"},       {"heart", "\xF0\x9F\x92\x9A"},
    {"heart", "\xF0\x9F\xA7\xA1"},       {"heart", "\xF0\x9F\x96\xA4"},
    {"broken", "\xF0\x9F\x92\x94"},      {"ring", "\xF0\x9F\x92\x8D"},
    {"rose", "\xF0\x9F\x8C\xB9"},        {"gem", "\xF0\x9F\x92\x8E"},
    // Hands and gestures.
    {"like", "\xF0\x9F\x91\x8D"},        {"good", "\xF0\x9F\x91\x8D"},
    {"ok", "\xF0\x9F\x91\x8C"},          {"ok", "\xF0\x9F\x86\x97"},
    {"no", "\xF0\x9F\x91\x8E"},          {"bad", "\xF0\x9F\x91\x8E"},
    {"clap", "\xF0\x9F\x91\x8F"},        {"wave", "\xF0\x9F\x91\x8B"},
    {"muscle", "\xF0\x9F\x92\xAA"},      {"fist", "\xF0\x9F\x91\x8A"},
    {"point", "\xF0\x9F\x91\x89"},       {"point", "\xF0\x9F\x91\x88"},
    {"up", "\xF0\x9F\x91\x86"},          {"down", "\xF0\x9F\x91\x87"},
    {"hand", "\xE2\x9C\x8B"},            {"hand", "\xF0\x9F\x91\x8B"},
    {"write", "\xE2\x9C\x8D"},           {"deal", "\xF0\x9F\xA4\x9D"},
    {"victory", "\xE2\x9C\x8C"},         {"luck", "\xF0\x9F\xA4\x9E"},
    {"call", "\xF0\x9F\xA4\x99"},        {"rock", "\xF0\x9F\xA4\x98"},
    // Animals.
    {"cat", "\xF0\x9F\x90\xB1"},         {"cat", "\xF0\x9F\x98\xB8"},
    {"cat", "\xF0\x9F\x98\xBB"},         {"dog", "\xF0\x9F\x90\xB6"},
    {"panda", "\xF0\x9F\x90\xBC"},       {"fox", "\xF0\x9F\xA6\x8A"},
    {"bear", "\xF0\x9F\x90\xBB"},        {"pig", "\xF0\x9F\x90\xB7"},
    {"rabbit", "\xF0\x9F\x90\xB0"},      {"mouse", "\xF0\x9F\x90\xAD"},
    {"tiger", "\xF0\x9F\x90\xAF"},       {"lion", "\xF0\x9F\xA6\x81"},
    {"monkey", "\xF0\x9F\x90\xB5"},      {"horse", "\xF0\x9F\x90\xB4"},
    {"cow", "\xF0\x9F\x90\xAE"},         {"chick", "\xF0\x9F\x90\xA5"},
    {"bird", "\xF0\x9F\x90\xA6"},        {"penguin", "\xF0\x9F\x90\xA7"},
    {"fish", "\xF0\x9F\x90\xA0"},        {"whale", "\xF0\x9F\x90\xB3"},
    {"dolphin", "\xF0\x9F\x90\xAC"},     {"turtle", "\xF0\x9F\x90\xA2"},
    {"snake", "\xF0\x9F\x90\x8D"},       {"bee", "\xF0\x9F\x90\x9D"},
    {"bug", "\xF0\x9F\x90\x9B"},         {"bug", "\xF0\x9F\x90\x9E"},
    {"butterfly", "\xF0\x9F\xA6\x8B"},   {"unicorn", "\xF0\x9F\xA6\x84"},
    {"dragon", "\xF0\x9F\x90\xB2"},      {"sheep", "\xF0\x9F\x90\x91"},
    {"frog", "\xF0\x9F\x90\xB8"},        {"duck", "\xF0\x9F\xA6\x86"},
    // Food and drink.
    {"food", "\xF0\x9F\x8D\x94"},        {"food", "\xF0\x9F\x8D\x9C"},
    {"food", "\xF0\x9F\x8D\x9A"},        {"rice", "\xF0\x9F\x8D\x9A"},
    {"noodle", "\xF0\x9F\x8D\x9C"},      {"burger", "\xF0\x9F\x8D\x94"},
    {"pizza", "\xF0\x9F\x8D\x95"},       {"fries", "\xF0\x9F\x8D\x9F"},
    {"sushi", "\xF0\x9F\x8D\xA3"},       {"bread", "\xF0\x9F\x8D\x9E"},
    {"egg", "\xF0\x9F\xA5\x9A"},         {"meat", "\xF0\x9F\x8D\x96"},
    {"cake", "\xF0\x9F\x8E\x82"},        {"cake", "\xF0\x9F\x8D\xB0"},
    {"candy", "\xF0\x9F\x8D\xAC"},       {"chocolate", "\xF0\x9F\x8D\xAB"},
    {"cookie", "\xF0\x9F\x8D\xAA"},      {"ice", "\xF0\x9F\x8D\xA6"},
    {"apple", "\xF0\x9F\x8D\x8E"},       {"banana", "\xF0\x9F\x8D\x8C"},
    {"grape", "\xF0\x9F\x8D\x87"},       {"melon", "\xF0\x9F\x8D\x89"},
    {"peach", "\xF0\x9F\x8D\x91"},       {"lemon", "\xF0\x9F\x8D\x8B"},
    {"berry", "\xF0\x9F\x8D\x93"},       {"tomato", "\xF0\x9F\x8D\x85"},
    {"coffee", "\xE2\x98\x95"},          {"tea", "\xF0\x9F\xA7\x8B"},
    {"beer", "\xF0\x9F\x8D\xBA"},{"wine", "\xF0\x9F\x8D\xB7"},
    {"drink", "\xF0\x9F\xA5\xA4"},       {"water", "\xF0\x9F\x92\xA7"},
    {"cheers", "\xF0\x9F\x8D\xBB"},      {"milk", "\xF0\x9F\xA5\x9B"},
    // Weather and nature.
    {"sun", "\xE2\x98\x80"},             {"sun", "\xF0\x9F\x8C\x9E"},
    {"moon", "\xF0\x9F\x8C\x99"},        {"moon", "\xF0\x9F\x8C\x9D"},
    {"rain", "\xF0\x9F\x8C\xA7"},        {"rain", "\xE2\x98\x94"},
    {"snow", "\xE2\x9D\x84"},            {"snow", "\xE2\x98\x83"},
    {"cloud", "\xE2\x98\x81"},           {"storm", "\xE2\x9B\x88"},
    {"wind", "\xF0\x9F\x8C\xAC"},        {"rainbow", "\xF0\x9F\x8C\x88"},
    {"fire", "\xF0\x9F\x94\xA5"},        {"flower", "\xF0\x9F\x8C\xB8"},
    {"flower", "\xF0\x9F\x8C\xBA"},      {"tree", "\xF0\x9F\x8C\xB3"},
    {"leaf", "\xF0\x9F\x8D\x83"},        {"cactus", "\xF0\x9F\x8C\xB5"},
    {"mountain", "\xE2\x9B\xB0"},        {"ocean", "\xF0\x9F\x8C\x8A"},
    {"earth", "\xF0\x9F\x8C\x8D"},       {"sparkle", "\xE2\x9C\xA8"},
    {"zap", "\xE2\x9A\xA1"},             {"comet", "\xE2\x98\x84"},
    // Objects and work.
    {"phone", "\xF0\x9F\x93\xB1"},       {"computer", "\xF0\x9F\x92\xBB"},
    {"code", "\xF0\x9F\x92\xBB"},        {"book", "\xF0\x9F\x93\x9A"},
    {"book", "\xF0\x9F\x93\x96"},        {"pen", "\xF0\x9F\x96\x8A"},
    {"pencil", "\xE2\x9C\x8F"},          {"note", "\xF0\x9F\x93\x9D"},
    {"mail", "\xF0\x9F\x93\xA7"},        {"file", "\xF0\x9F\x93\x81"},
    {"chart", "\xF0\x9F\x93\x8A"},       {"money", "\xF0\x9F\x92\xB0"},
    {"money", "\xF0\x9F\x92\xB5"},       {"card", "\xF0\x9F\x92\xB3"},
    {"gift", "\xF0\x9F\x8E\x81"},        {"bell", "\xF0\x9F\x94\x94"},
    {"lock", "\xF0\x9F\x94\x92"},        {"key", "\xF0\x9F\x94\x91"},
    {"search", "\xF0\x9F\x94\x8D"},      {"idea", "\xF0\x9F\x92\xA1"},
    {"tool", "\xF0\x9F\x94\xA7"},        {"gear", "\xE2\x9A\x99"},
    {"link", "\xF0\x9F\x94\x97"},        {"camera", "\xF0\x9F\x93\xB7"},
    {"video", "\xF0\x9F\x8E\xA5"},       {"music", "\xF0\x9F\x8E\xB5"},
    {"music", "\xF0\x9F\x8E\xB6"},       {"game", "\xF0\x9F\x8E\xAE"},
    {"ball", "\xE2\x9A\xBD"},            {"trophy", "\xF0\x9F\x8F\x86"},
    {"medal", "\xF0\x9F\x8E\x96"},       {"crown", "\xF0\x9F\x91\x91"},
    {"clock", "\xF0\x9F\x95\x92"},       {"time", "\xE2\x8F\xB0"},
    {"calendar", "\xF0\x9F\x93\x85"},    {"pin", "\xF0\x9F\x93\x8C"},
    {"work", "\xF0\x9F\x92\xBC"},        {"target", "\xF0\x9F\x8E\xAF"},
    // Travel and places.
    {"rocket", "\xF0\x9F\x9A\x80"},      {"plane", "\xE2\x9C\x88"},
    {"car", "\xF0\x9F\x9A\x97"},         {"train", "\xF0\x9F\x9A\x86"},
    {"bike", "\xF0\x9F\x9A\xB2"},        {"ship", "\xF0\x9F\x9A\xA2"},
    {"bus", "\xF0\x9F\x9A\x8C"},         {"home", "\xF0\x9F\x8F\xA0"},
    {"office", "\xF0\x9F\x8F\xA2"},      {"school", "\xF0\x9F\x8F\xAB"},
    {"hospital", "\xF0\x9F\x8F\xA5"},    {"hotel", "\xF0\x9F\x8F\xA8"},
    {"map", "\xF0\x9F\x97\xBA"},         {"flag", "\xF0\x9F\x9A\xA9"},
    // Symbols and status.
    {"check", "\xE2\x9C\x85"},           {"check", "\xE2\x9C\x94"},
    {"done", "\xE2\x9C\x85"},            {"cross", "\xE2\x9D\x8C"},
    {"cross", "\xE2\x9C\x96"},           {"warn", "\xE2\x9A\xA0"},
    {"stop", "\xF0\x9F\x9B\x91"},        {"question", "\xE2\x9D\x93"},
    {"exclaim", "\xE2\x9D\x97"},         {"info", "\xE2\x84\xB9"},
    {"new", "\xF0\x9F\x86\x95"},         {"free", "\xF0\x9F\x86\x93"},
    {"top", "\xF0\x9F\x94\xBC"},         {"hot", "\xF0\x9F\x94\xA5"},
    {"bomb", "\xF0\x9F\x92\xA3"},        {"skull", "\xF0\x9F\x92\x80"},
    {"ghost", "\xF0\x9F\x91\xBB"},       {"alien", "\xF0\x9F\x91\xBD"},
    {"robot", "\xF0\x9F\xA4\x96"},       {"poop", "\xF0\x9F\x92\xA9"},
    {"clown", "\xF0\x9F\xA4\xA1"},       {"devil", "\xF0\x9F\x98\x88"},
    {"angel", "\xF0\x9F\x98\x87"},       {"eyes", "\xF0\x9F\x91\x80"},
    {"eye", "\xF0\x9F\x91\x81"},         {"ear", "\xF0\x9F\x91\x82"},
    {"brain", "\xF0\x9F\xA7\xA0"},       {"bone", "\xF0\x9F\xA6\xB4"},
    {"recycle", "\xE2\x99\xBB"},         {"cycle", "\xF0\x9F\x94\x84"},
    {"back", "\xF0\x9F\x94\x99"},        {"soon", "\xF0\x9F\x94\x9C"},
    {"end", "\xF0\x9F\x94\x9A"},         {"plus", "\xE2\x9E\x95"},
    {"minus", "\xE2\x9E\x96"},           {"arrowup", "\xE2\xAC\x86"},
    {"arrowdown", "\xE2\xAC\x87"},       {"arrowleft", "\xE2\xAC\x85"},
    {"arrowright", "\xE2\x9E\xA1"},      {"zzz", "\xF0\x9F\x92\xA4"},
    {"boom", "\xF0\x9F\x92\xA5"},        {"tada", "\xF0\x9F\x8E\x89"},
    {"balloon", "\xF0\x9F\x8E\x88"},     {"confetti", "\xF0\x9F\x8E\x8A"},
    {"birthday", "\xF0\x9F\x8E\x82"},    {"christmas", "\xF0\x9F\x8E\x84"},
    {"firework", "\xF0\x9F\x8E\x86"},    {"lantern", "\xF0\x9F\x8F\xAE"},
    {"envelope", "\xF0\x9F\xA7\xA8"},    {"light", "\xF0\x9F\x92\xA1"},
}};

bool narrow_ascii(const std::wstring_view input, std::string& output) {
    if (std::any_of(input.begin(), input.end(),
                    [](const wchar_t value) { return value > 0x7f; })) return false;
    output.assign(input.size(), '\0');
    std::transform(input.begin(), input.end(), output.begin(),
                   [](const wchar_t value) { return static_cast<char>(value); });
    return !output.empty();
}

bool parse_arguments(const int argc, wchar_t** argv, Arguments& result) {
    if (argc != 7 || std::wstring_view(argv[1]) != L"--owo-plugin-pipe" ||
        std::wstring_view(argv[3]) != L"--owo-plugin-id" ||
        std::wstring_view(argv[5]) != L"--owo-plugin-data") return false;
    result.pipe_name = argv[2];
    result.data_path = argv[6];
    return !result.pipe_name.empty() && !result.data_path.empty() &&
           narrow_ascii(argv[4], result.plugin_id);
}

bool sandbox_is_active() {
    HANDLE token = nullptr;
    DWORD is_appcontainer = 0;
    DWORD returned = 0;
    const bool queried = OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token) != FALSE &&
        GetTokenInformation(token, TokenIsAppContainer, &is_appcontainer,
                            sizeof(is_appcontainer), &returned) != FALSE;
    if (token != nullptr) CloseHandle(token);
    BOOL in_job = FALSE;
    return queried && is_appcontainer != 0 &&
           IsProcessInJob(GetCurrentProcess(), nullptr, &in_job) != FALSE && in_job != FALSE;
}

// Normalize a query keyword: trim ASCII whitespace and lowercase. Rejects non-ASCII.
bool normalize_keyword(const std::string_view payload, std::string& keyword) {
    std::size_t begin = 0;
    std::size_t end = payload.size();
    while (begin < end && (payload[begin] == ' ' || payload[begin] == '\t')) ++begin;
    while (end > begin && (payload[end - 1] == ' ' || payload[end - 1] == '\t')) --end;
    if (begin >= end || end - begin > 32) return false;
    keyword.clear();
    for (std::size_t index = begin; index < end; ++index) {
        const auto byte = static_cast<unsigned char>(payload[index]);
        if (byte >= 0x80U) return false;
        keyword.push_back(static_cast<char>(byte >= 'A' && byte <= 'Z' ? byte + ('a' - 'A') : byte));
    }
    return true;
}

// Build newline-separated emoji candidates for a keyword (prefix match), bounded to nine.
std::string query_emoji(const std::string_view keyword) {
    std::string result;
    std::size_t count = 0;
    for (const auto& entry : kEmojiTable) {
        if (count >= 9) break;
        if (entry.keyword.substr(0, keyword.size()) == keyword) {
            if (!result.empty()) result.push_back('\n');
            result.append(entry.emoji);
            ++count;
        }
    }
    return result;
}

int run_plugin(const Arguments& arguments) {
    if (!sandbox_is_active()) return 10;

    auto connected = owo::plugin::connect_plugin_pipe_client(
        arguments.pipe_name, std::chrono::seconds(5));
    if (!connected) return 20;

    owo::plugin::PluginMessage hello;
    hello.type = owo::plugin::PluginMessageType::hello_request;
    hello.status = owo::plugin::PluginStatus::success;
    hello.request_id = 1;
    hello.plugin_id = arguments.plugin_id;
    if (!owo::plugin::send_plugin_pipe_message(
            connected.pipe, hello, std::chrono::seconds(5)).ok) return 21;
    const auto response = owo::plugin::receive_plugin_pipe_message(
        connected.pipe, std::chrono::seconds(5));
    if (!response.status.ok ||
        response.message.type != owo::plugin::PluginMessageType::hello_response ||
        response.message.request_id != hello.request_id ||
        response.message.plugin_id != arguments.plugin_id ||
        response.message.capabilities !=
            std::vector<std::string>{"cancel.v1", "invoke.v1"}) return 22;

    std::mutex send_mutex;
    const auto send = [&](const owo::plugin::PluginMessage& message) {
        std::lock_guard lock(send_mutex);
        return owo::plugin::send_plugin_pipe_message(
            connected.pipe, message, std::chrono::seconds(5)).ok;
    };
    const auto response_for = [&](const owo::plugin::PluginMessage& request,
                                  const owo::plugin::PluginStatus status,
                                  std::string payload, std::string diagnostic) {
        owo::plugin::PluginMessage result;
        result.type = owo::plugin::PluginMessageType::invoke_response;
        result.status = status;
        result.request_id = request.request_id;
        result.plugin_id = arguments.plugin_id;
        result.payload = std::move(payload);
        result.diagnostic = std::move(diagnostic);
        return result;
    };

    for (;;) {
        const auto request = owo::plugin::receive_plugin_pipe_message(
            connected.pipe, std::chrono::hours(24));
        if (!request.status.ok) return 23;
        if (request.message.plugin_id != arguments.plugin_id) return 24;
        if (request.message.type == owo::plugin::PluginMessageType::shutdown_request) {
            owo::plugin::PluginMessage acknowledgement;
            acknowledgement.type = owo::plugin::PluginMessageType::acknowledgement;
            acknowledgement.status = owo::plugin::PluginStatus::success;
            acknowledgement.request_id = request.message.request_id;
            acknowledgement.plugin_id = arguments.plugin_id;
            if (!send(acknowledgement)) return 25;
            return 0;
        }
        if (request.message.type == owo::plugin::PluginMessageType::cancel_request) {
            // Emoji queries complete synchronously, so there is never an active invocation
            // to cancel; acknowledge only when the target matches an in-flight id (none).
            continue;
        }
        if (request.message.type != owo::plugin::PluginMessageType::invoke_request) return 28;
        if (request.message.service != "emoji.query.v1") {
            auto unsupported = response_for(request.message,
                owo::plugin::PluginStatus::invalid_request, {}, "unknown versioned service");
            if (!send(unsupported)) return 30;
            continue;
        }
        std::string keyword;
        if (!normalize_keyword(request.message.payload, keyword)) {
            auto invalid = response_for(request.message,
                owo::plugin::PluginStatus::invalid_request, {}, "invalid emoji keyword");
            if (!send(invalid)) return 31;
            continue;
        }
        auto candidates = query_emoji(keyword);
        auto result = response_for(request.message, owo::plugin::PluginStatus::success,
                                   std::move(candidates), {});
        if (!send(result)) return 32;
    }
}

}  // namespace

int wmain(const int argc, wchar_t** argv) {
    Arguments arguments;
    if (!parse_arguments(argc, argv, arguments)) return 1;
    return run_plugin(arguments);
}
