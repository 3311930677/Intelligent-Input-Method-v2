#pragma once

#include "owo/protocol/envelope.h"

#include <chrono>
#include <string>
#include <string_view>

namespace owo::ipc {

inline constexpr wchar_t kCorePipeName[] = LR"(\\.\pipe\OwO.InputMethod.Core.P1)";

struct ExchangeResult {
    protocol::ValidationResult status;
    std::string response;
};

/// 通过命名管道发送单个请求并等待单个响应。
/// @thread_safety 可并发调用；每次调用使用独立句柄。
[[nodiscard]] ExchangeResult exchange(
    const wchar_t* pipe_name,
    std::string_view request,
    std::chrono::milliseconds timeout);

/// 运行单连接串行服务循环。仅供 P1 Core Service 原型使用。
[[nodiscard]] int run_core_server(const wchar_t* pipe_name);

}  // namespace owo::ipc

