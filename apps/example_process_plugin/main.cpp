#include "owo/plugin/plugin_pipe.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <string>
#include <string_view>

namespace {

struct Arguments {
    std::wstring pipe_name;
    std::string plugin_id;
    std::filesystem::path data_path;
};

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

bool sensitive_environment_is_absent() {
    wchar_t value[2]{};
    SetLastError(ERROR_SUCCESS);
    return GetEnvironmentVariableW(L"OWO_TEST_SECRET", value,
                                   static_cast<DWORD>(std::size(value))) == 0 &&
           GetLastError() == ERROR_ENVVAR_NOT_FOUND;
}

bool installed_directory_is_read_only() {
    wchar_t executable[32768]{};
    const auto length = GetModuleFileNameW(nullptr, executable,
                                           static_cast<DWORD>(std::size(executable)));
    if (length == 0 || length == std::size(executable)) return false;
    const auto probe = std::filesystem::path(executable).parent_path() /
                       L"should-not-write.tmp";
    HANDLE file = CreateFileW(probe.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW,
                              FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return true;
    CloseHandle(file);
    DeleteFileW(probe.c_str());
    return false;
}

bool write_data_probe(const std::filesystem::path& data_path) {
    const auto marker = data_path / L"probe-data.txt";
    HANDLE file = CreateFileW(marker.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                              FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return false;
    constexpr std::string_view contents = "sandbox-data-ok\n";
    DWORD written = 0;
    const bool ok = WriteFile(file, contents.data(), static_cast<DWORD>(contents.size()),
                              &written, nullptr) != FALSE && written == contents.size();
    CloseHandle(file);
    return ok;
}

int run_plugin(const Arguments& arguments) {
    if (!sandbox_is_active()) return 10;
    if (!sensitive_environment_is_absent()) return 11;
    wchar_t environment_data[32768]{};
    const auto environment_length = GetEnvironmentVariableW(
        L"OWO_PLUGIN_DATA", environment_data,
        static_cast<DWORD>(std::size(environment_data)));
    if (environment_length == 0 || environment_length >= std::size(environment_data) ||
        std::filesystem::path(environment_data).lexically_normal() !=
            arguments.data_path.lexically_normal()) return 14;
    if (!installed_directory_is_read_only()) return 12;
    if (!write_data_probe(arguments.data_path)) return 13;

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
        !response.message.capabilities.empty()) return 22;

    for (;;) {
        const auto request = owo::plugin::receive_plugin_pipe_message(
            connected.pipe, std::chrono::hours(24));
        if (!request.status.ok) return 23;
        if (request.message.type != owo::plugin::PluginMessageType::shutdown_request ||
            request.message.plugin_id != arguments.plugin_id) return 24;
        owo::plugin::PluginMessage acknowledgement;
        acknowledgement.type = owo::plugin::PluginMessageType::acknowledgement;
        acknowledgement.status = owo::plugin::PluginStatus::success;
        acknowledgement.request_id = request.message.request_id;
        acknowledgement.plugin_id = arguments.plugin_id;
        if (!owo::plugin::send_plugin_pipe_message(
                connected.pipe, acknowledgement, std::chrono::seconds(5)).ok) return 25;
        return 0;
    }
}

}  // namespace

int wmain(const int argc, wchar_t** argv) {
    Arguments arguments;
    if (!parse_arguments(argc, argv, arguments)) return 1;
    return run_plugin(arguments);
}
