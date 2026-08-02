#pragma once

#include "owo/plugin/plugin_manifest.h"

#include <chrono>
#include <filesystem>
#include <memory>
#include <string>
#include <string_view>

namespace owo::plugin {

struct PluginHostOperationResult {
    bool ok{};
    bool forced_termination{};
    std::string diagnostic;
};

class PluginHostSession {
public:
    PluginHostSession();
    PluginHostSession(const PluginHostSession&) = delete;
    PluginHostSession& operator=(const PluginHostSession&) = delete;
    PluginHostSession(PluginHostSession&& other) noexcept;
    PluginHostSession& operator=(PluginHostSession&& other) noexcept;
    ~PluginHostSession();

    [[nodiscard]] bool valid() const noexcept;
    [[nodiscard]] std::string_view plugin_id() const noexcept;
    [[nodiscard]] std::string_view version() const noexcept;
    [[nodiscard]] unsigned long process_id() const noexcept;

    /// Requests protocol shutdown and waits for both acknowledgement and process exit.
    /// Any protocol, timeout, or process failure terminates the entire plugin Job.
    [[nodiscard]] PluginHostOperationResult shutdown(std::chrono::milliseconds timeout);

    /// Immediately terminates the entire plugin Job. Repeated calls are harmless.
    void terminate() noexcept;

private:
    struct Impl;
    explicit PluginHostSession(std::unique_ptr<Impl> implementation);
    std::unique_ptr<Impl> implementation_;

    friend struct PluginHostLaunchResult;
    friend PluginHostLaunchResult launch_active_plugin(
        const std::filesystem::path&, std::string_view, std::chrono::milliseconds);
};

struct PluginHostLaunchResult {
    PluginHostSession session;
    PluginManifest manifest;
    std::filesystem::path installed_path;
    std::filesystem::path data_path;
    std::string diagnostic;

    [[nodiscard]] explicit operator bool() const noexcept {
        return session.valid() && diagnostic.empty();
    }
};

/// Revalidates and launches the active installed process plugin in its zero-capability sandbox.
/// The plugin must send a valid hello_request before this call succeeds.
[[nodiscard]] PluginHostLaunchResult launch_active_plugin(
    const std::filesystem::path& plugin_store_root,
    std::string_view plugin_id,
    std::chrono::milliseconds startup_timeout);

}  // namespace owo::plugin
