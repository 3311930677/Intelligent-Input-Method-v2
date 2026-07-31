#include "owo/ipc/named_pipe.h"
#include "owo/protocol/messages.h"

#include <Windows.h>

#include <chrono>
#include <iostream>
#include <string>
#include <thread>

namespace {

bool exchange_and_check(const std::uint64_t request_id,
                        const std::uint64_t generation,
                        const owo::protocol::MessageType type,
                        const std::string& expected) {
    const owo::protocol::Message request{type, request_id, generation, "nihao"};
    for (int attempt = 0; attempt < 100; ++attempt) {
        const auto result = owo::ipc::exchange(
            owo::ipc::kCorePipeName, owo::protocol::encode_message(request),
            std::chrono::milliseconds(100));
        if (result.status) {
            const auto decoded = owo::protocol::decode_message(result.response);
            return decoded.validation && decoded.message.request_id == request_id &&
                   decoded.message.context_generation == generation &&
                   decoded.message.text == expected;
        }
        Sleep(20);
    }
    return false;
}

}  // namespace

int wmain(int argc, wchar_t** argv) {
    if (argc != 2) return 2;

    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION process{};
    std::wstring command = L"\"" + std::wstring(argv[1]) + L"\"";
    if (!CreateProcessW(nullptr, command.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW,
                        nullptr, nullptr, &startup, &process)) {
        std::cerr << "CreateProcessW failed: " << GetLastError() << '\n';
        return 3;
    }
    CloseHandle(process.hThread);

    const bool first = exchange_and_check(
        10, 1, owo::protocol::MessageType::candidate_request, "固定候选");
    const bool second = exchange_and_check(
        11, 2, owo::protocol::MessageType::candidate_request, "固定候选");
    const bool shutdown = exchange_and_check(
        12, 3, owo::protocol::MessageType::shutdown_request, "shutdown_ack");
    const DWORD wait_result = WaitForSingleObject(process.hProcess, 3000);
    DWORD exit_code = STILL_ACTIVE;
    GetExitCodeProcess(process.hProcess, &exit_code);
    if (wait_result != WAIT_OBJECT_0) {
        // 仅终止本测试创建且未能通过协议关停的子进程，避免污染后续测试。
        TerminateProcess(process.hProcess, 5);
        WaitForSingleObject(process.hProcess, 1000);
    }
    CloseHandle(process.hProcess);

    const auto missing = owo::ipc::exchange(
        LR"(\\.\pipe\OwO.InputMethod.IntentionallyMissing)", "{}",
        std::chrono::milliseconds(10));
    const bool disconnected = !missing.status;

    constexpr wchar_t stalled_pipe_name[] = LR"(\\.\pipe\OwO.InputMethod.StalledTest)";
    const HANDLE ready = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    std::jthread stalled_server([ready] {
        const HANDLE pipe = CreateNamedPipeW(
            stalled_pipe_name, PIPE_ACCESS_DUPLEX,
            PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT, 1, 4096, 4096, 0, nullptr);
        SetEvent(ready);
        if (pipe != INVALID_HANDLE_VALUE) {
            ConnectNamedPipe(pipe, nullptr);
            Sleep(250);
            DisconnectNamedPipe(pipe);
            CloseHandle(pipe);
        }
    });
    WaitForSingleObject(ready, 1000);
    CloseHandle(ready);
    const auto timeout_start = std::chrono::steady_clock::now();
    const auto stalled = owo::ipc::exchange(
        stalled_pipe_name,
        owo::protocol::encode_message({owo::protocol::MessageType::candidate_request,
                                       20, 4, "stall"}),
        std::chrono::milliseconds(30));
    const auto timeout_elapsed = std::chrono::steady_clock::now() - timeout_start;
    const bool response_timeout = !stalled.status &&
        stalled.status.error == owo::protocol::ErrorCode::timeout &&
        timeout_elapsed < std::chrono::milliseconds(200);

    return first && second && shutdown && disconnected && response_timeout &&
                   wait_result == WAIT_OBJECT_0 && exit_code == 0
               ? 0
               : 4;
}
