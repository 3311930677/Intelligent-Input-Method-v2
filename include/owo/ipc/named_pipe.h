#pragma once

#include "owo/protocol/envelope.h"

#include <chrono>
#include <cstdint>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

namespace owo::engine {
class Lexicon;
class UserFrequencyStore;
}

namespace owo::config {
class ConfigMonitor;
}

namespace owo::model {
class IModelBackend;
}

namespace owo::ipc {

inline constexpr wchar_t kCorePipeName[] = LR"(\\.\pipe\OwO.InputMethod.Core.P1)";
inline constexpr wchar_t kModelHostPipeName[] = LR"(\\.\pipe\OwO.InputMethod.ModelHost.P3.v1)";

struct ExchangeResult {
    protocol::ValidationResult status;
    std::string response;
};

struct PluginCandidateResult {
    bool ok{};
    std::vector<std::string> candidates;
    std::string diagnostic;
};

using PluginCandidateProvider = std::function<PluginCandidateResult(
    std::string_view query, std::chrono::milliseconds timeout)>;

enum class VoiceSessionCommand : std::uint8_t { start, poll, cancel };
enum class VoiceSessionState : std::uint8_t { idle, listening, final_result, failed, cancelled };

struct VoiceSessionResult {
    bool ok{};
    VoiceSessionState state{VoiceSessionState::idle};
    std::string text;
    std::string diagnostic;
};

using VoiceSessionProvider = std::function<VoiceSessionResult(
    VoiceSessionCommand command, std::string_view owner, std::string_view language,
    std::chrono::milliseconds timeout)>;

/// 通过命名管道发送单个请求并等待单个响应。
/// @thread_safety 可并发调用；每次调用使用独立句柄。
[[nodiscard]] ExchangeResult exchange(
    const wchar_t* pipe_name,
    std::string_view request,
    std::chrono::milliseconds timeout);

/// 运行单连接串行服务循环，使用内置开发降级词典。
[[nodiscard]] int run_core_server(const wchar_t* pipe_name);

/// 运行使用显式词典的服务循环。词典在服务退出前必须保持有效。
[[nodiscard]] int run_core_server(const wchar_t* pipe_name, const engine::Lexicon& lexicon);
[[nodiscard]] int run_core_server(const wchar_t* pipe_name,
                                  const engine::Lexicon& lexicon,
                                  engine::UserFrequencyStore* user_frequency);
[[nodiscard]] int run_core_server(const wchar_t* pipe_name,
                                  const engine::Lexicon& lexicon,
                                  engine::UserFrequencyStore* user_frequency,
                                  const wchar_t* model_pipe_name);
[[nodiscard]] int run_core_server(const wchar_t* pipe_name,
                                  const engine::Lexicon& lexicon,
                                  engine::UserFrequencyStore* user_frequency,
                                  const wchar_t* model_pipe_name,
                                  const config::ConfigMonitor* config_monitor,
                                  const PluginCandidateProvider* plugin_candidates = nullptr,
                                  const VoiceSessionProvider* voice_sessions = nullptr);

/// 运行 ModelHost v1 串行服务循环。后端在服务退出前必须保持有效。
[[nodiscard]] int run_model_server(const wchar_t* pipe_name, model::IModelBackend& backend);

}  // namespace owo::ipc
