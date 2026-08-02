#include "owo/plugin/plugin_host.h"
#include "owo/plugin/plugin_sandbox.h"
#include "owo/plugin/plugin_store.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>

namespace {

bool write_file(const std::filesystem::path& path, const std::string_view contents) {
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    output.write(contents.data(), static_cast<std::streamsize>(contents.size()));
    return static_cast<bool>(output);
}

std::string read_file(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}

std::string manifest_json(const std::string_view plugin_id) {
    return "{\"id\":\"" + std::string(plugin_id) +
        "\",\"name\":\"Runtime Test\",\"version\":\"1.0.0\","
        "\"api_version\":1,\"runtime\":\"process\","
        "\"entry\":\"bin/example-process-plugin.exe\",\"permissions\":[],"
        "\"network\":false,\"config_schema\":\"config.schema.json\"}";
}

}  // namespace

int main(const int argc, char** argv) {
    if (argc != 3) return 1;
    const std::filesystem::path root(argv[1]);
    const std::filesystem::path probe(argv[2]);
    const auto plugin_id = "owo.plugin.runtime-test-" +
        std::to_string(GetCurrentProcessId()) + "-" + std::to_string(GetTickCount64());
    std::error_code error;
    std::filesystem::remove_all(root, error);
    if (error || !std::filesystem::is_regular_file(probe)) return 2;

    const auto finish = [&](const int code) {
        std::filesystem::remove_all(root, error);
        const auto profile = owo::plugin::prepare_plugin_sandbox_profile(plugin_id);
        if (!profile.ok) return 90;
        const auto deleted = owo::plugin::delete_plugin_sandbox_profile(profile.profile_name);
        return deleted.ok && !std::filesystem::exists(root) ? code : 91;
    };

    const auto initialized = owo::plugin::initialize_plugin_store(root);
    if (!initialized.ok) return finish(3);
    const auto staging = root / L"staging" / L".runtime-v1";
    std::filesystem::create_directories(staging / L"bin", error);
    if (error || !CopyFileW(probe.c_str(),
                            (staging / L"bin" / L"example-process-plugin.exe").c_str(),
                            TRUE)) return finish(4);
    if (!write_file(staging / L"manifest.json", manifest_json(plugin_id)) ||
        !write_file(staging / L"config.schema.json", "{}")) return finish(5);
    const auto published = owo::plugin::publish_staged_plugin(
        root, staging, std::string(64, 'a'), std::string(64, 'b'));
    if (!published.ok || !published.activated) return finish(6);
    const auto active = owo::plugin::query_active_plugin_version(root, plugin_id);
    if (!active.ok || active.manifest.version != "1.0.0") return finish(7);

    SetEnvironmentVariableW(L"OWO_TEST_SECRET", L"must-not-leak");
    auto launched = owo::plugin::launch_active_plugin(
        root, plugin_id, std::chrono::seconds(5));
    SetEnvironmentVariableW(L"OWO_TEST_SECRET", nullptr);
    if (!launched) {
        std::cerr << "PluginHost launch failed: " << launched.diagnostic << '\n';
        return finish(8);
    }
    if (!launched.session.valid() || launched.session.plugin_id() != plugin_id ||
        launched.session.version() != "1.0.0" || launched.session.process_id() == 0 ||
        launched.installed_path != published.installed_path ||
        launched.data_path != root / L"data" / std::filesystem::path(plugin_id))
        return finish(9);
    if (read_file(launched.data_path / L"probe-data.txt") != "sandbox-data-ok\n" ||
        std::filesystem::exists(launched.installed_path / L"bin" /
                                L"should-not-write.tmp")) return finish(10);
    const auto shutdown = launched.session.shutdown(std::chrono::seconds(2));
    if (!shutdown.ok || shutdown.forced_termination || launched.session.valid())
        return finish(11);

    DWORD forced_pid = 0;
    HANDLE forced_process = nullptr;
    {
        auto forced = owo::plugin::launch_active_plugin(
            root, plugin_id, std::chrono::seconds(5));
        if (!forced) {
            std::cerr << "second PluginHost launch failed: " << forced.diagnostic << '\n';
            return finish(12);
        }
        forced_pid = forced.session.process_id();
        forced_process = OpenProcess(SYNCHRONIZE, FALSE, forced_pid);
        if (forced_process == nullptr) return finish(13);
    }
    const auto forced_wait = WaitForSingleObject(forced_process, 3000);
    CloseHandle(forced_process);
    if (forced_wait != WAIT_OBJECT_0) return finish(14);
    if (owo::plugin::launch_active_plugin(
            root, "../escape", std::chrono::seconds(1))) return finish(15);
    return finish(0);
}
