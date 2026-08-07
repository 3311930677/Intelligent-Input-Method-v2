#include "owo/ipc/named_pipe.h"
#include "owo/voice/voice_protocol.h"
#include "owo/config/config_monitor.h"
#include "owo/config/config_paths.h"
#include "owo/core/plugin_executor.h"
#include "owo/engine/binary_lexicon.h"
#include "owo/engine/user_frequency.h"

#include <Windows.h>
#include <psapi.h>
#pragma comment(lib, "psapi.lib")

#include <algorithm>
#include <condition_variable>
#include <cstdint>
#include <chrono>
#include <filesystem>
#include <future>
#include <iostream>
#include <mutex>
#include <string>
#include <string_view>
#include <thread>
#include <utility>

namespace {

std::filesystem::path sibling_executable(const wchar_t* name) {
    std::wstring path(32768, L'\0');
    const DWORD length = GetModuleFileNameW(nullptr, path.data(),
                                            static_cast<DWORD>(path.size()));
    if (length == 0 || length >= path.size()) return {};
    path.resize(length);
    return std::filesystem::path(path).parent_path() / name;
}

bool valid_language_for_command_line(const std::string_view language) {
    if (language.size() < 2 || language.size() > 35 || language.front() == '-' ||
        language.back() == '-') return false;
    bool previous_dash = false;
    for (const unsigned char byte : language) {
        const bool valid = (byte >= 'a' && byte <= 'z') || (byte >= 'A' && byte <= 'Z') ||
                           (byte >= '0' && byte <= '9') || byte == '-';
        if (!valid || (byte == '-' && previous_dash)) return false;
        previous_dash = byte == '-';
    }
    return true;
}

class VoiceProcessController final {
public:
    VoiceProcessController()
        : executable_(sibling_executable(L"owo_voice_input_plugin.exe")) {}

    ~VoiceProcessController() {
        shutdown_broker();
    }

    owo::ipc::VoiceSessionResult invoke(
        const owo::ipc::VoiceSessionCommand command, const std::string_view owner,
        const std::string_view language, const std::chrono::milliseconds timeout) {
        if (command == owo::ipc::VoiceSessionCommand::start)
            return start(owner, language, timeout);
        if (command == owo::ipc::VoiceSessionCommand::cancel) return cancel(owner);
        return snapshot(owner);
    }

private:
    void shutdown_broker() {
        if (process_ == nullptr) return;
        {
            std::lock_guard lock(write_mutex_);
            owo::voice::VoiceMessage shutdown;
            shutdown.type = owo::voice::VoiceMessageType::shutdown_request;
            shutdown.status = owo::voice::VoiceStatus::success;
            shutdown.request_id = ++request_id_;
            shutdown.plugin_id = std::string(owo::voice::kVoicePluginId);
            write_voice_message(shutdown);
        }
        reader_.request_stop();
        TerminateProcess(process_, ERROR_CANCELLED);
        if (reader_.joinable()) reader_.join();
        CloseHandle(process_);
        process_ = nullptr;
        CloseHandle(stdin_write_);
        stdin_write_ = nullptr;
        CloseHandle(stdout_read_);
        stdout_read_ = nullptr;
    }

    bool ensure_broker(std::string& diagnostic) {
        if (process_ != nullptr) return true;
        if (executable_.empty() || !std::filesystem::is_regular_file(executable_)) {
            diagnostic = "voice module executable is missing";
            return false;
        }
        SECURITY_ATTRIBUTES sa{sizeof(sa), nullptr, TRUE};
        HANDLE stdin_read = nullptr;
        HANDLE stdout_write = nullptr;
        if (!CreatePipe(&stdin_read, &stdin_write_, &sa, 0) ||
            !CreatePipe(&stdout_read_, &stdout_write, &sa, 0)) {
            diagnostic = "cannot create voice broker pipes";
            if (stdin_read) CloseHandle(stdin_read);
            if (stdin_write_) { CloseHandle(stdin_write_); stdin_write_ = nullptr; }
            if (stdout_read_) { CloseHandle(stdout_read_); stdout_read_ = nullptr; }
            if (stdout_write) CloseHandle(stdout_write);
            return false;
        }
        SetHandleInformation(stdin_write_, HANDLE_FLAG_INHERIT, 0);
        SetHandleInformation(stdout_read_, HANDLE_FLAG_INHERIT, 0);

        HANDLE stderr_read = nullptr;
        HANDLE stderr_write = nullptr;
        CreatePipe(&stderr_read, &stderr_write, &sa, 0);

        STARTUPINFOW startup{sizeof(startup)};
        startup.dwFlags = STARTF_USESTDHANDLES;
        startup.hStdInput = stdin_read;
        startup.hStdOutput = stdout_write;
        startup.hStdError = stderr_write ? stderr_write : stdout_write;

        std::wstring command = L"\"" + executable_.wstring() + L"\" --stdio";
        PROCESS_INFORMATION pi{};
        const BOOL created = CreateProcessW(
            executable_.c_str(), command.data(), nullptr, nullptr,
            TRUE, CREATE_NO_WINDOW, nullptr,
            executable_.parent_path().c_str(), &startup, &pi);
        CloseHandle(stdin_read);
        CloseHandle(stdout_write);
        if (stderr_read) CloseHandle(stderr_read);
        if (stderr_write) CloseHandle(stderr_write);

        if (!created) {
            diagnostic = "cannot start voice module: Win32 " + std::to_string(GetLastError());
            CloseHandle(stdin_write_); stdin_write_ = nullptr;
            CloseHandle(stdout_read_); stdout_read_ = nullptr;
            return false;
        }
        CloseHandle(pi.hThread);
        process_ = pi.hProcess;

        handshake_received_ = false;
        handshake_complete_ = false;
        reader_ = std::jthread([this](const std::stop_token token) {
            reader_loop(token);
        });

        {
            std::unique_lock lock(mutex_);
            if (!handshake_cv_.wait_for(lock, std::chrono::seconds(5),
                                        [this] { return handshake_received_; })) {
                diagnostic = "voice module handshake timed out";
                return false;
            }
        }

        {
            std::lock_guard lock(write_mutex_);
            owo::voice::VoiceMessage hello_resp;
            hello_resp.type = owo::voice::VoiceMessageType::hello_response;
            hello_resp.status = owo::voice::VoiceStatus::success;
            hello_resp.request_id = 1;
            hello_resp.plugin_id = std::string(owo::voice::kVoicePluginId);
            hello_resp.capabilities = {
                std::string(owo::voice::kMicrophoneCapability),
                std::string(owo::voice::kTranscribeCapability)};
            if (!write_voice_message(hello_resp)) {
                diagnostic = "cannot complete voice handshake";
                return false;
            }
        }
        handshake_complete_ = true;
        return true;
    }

    owo::ipc::VoiceSessionResult start(
        const std::string_view owner, const std::string_view language,
        const std::chrono::milliseconds timeout) {

        std::string diagnostic;
        if (!ensure_broker(diagnostic))
            return {false, owo::ipc::VoiceSessionState::idle, {}, std::move(diagnostic)};

        if (!valid_language_for_command_line(language))
            return {false, owo::ipc::VoiceSessionState::idle, {},
                    "invalid language tag"};

        // Preempt current session if active (send cancel, wait for ack)
        if (session_state_ == owo::ipc::VoiceSessionState::listening)
            send_cancel_and_wait();

        using namespace std::chrono;
        const auto now_ms = duration_cast<milliseconds>(
            system_clock::now().time_since_epoch()).count();
        owo::voice::CapabilityGrant grant;
        grant.grant_id = "g-" + std::to_string(++grant_counter_);
        grant.subject = std::string(owo::voice::kVoicePluginId);
        grant.scope = std::string(owo::voice::kActiveCompositionScope);
        grant.expires_at_unix_ms = static_cast<std::uint64_t>(now_ms) + 5U * 60U * 1000U;
        grant.max_uses = 1;
        grant.risk_level = owo::voice::RiskLevel::r3;
        grant.capabilities = {std::string(owo::voice::kMicrophoneCapability)};

        const auto clamped_timeout = static_cast<std::uint32_t>(
            std::clamp<std::uint64_t>(timeout.count(), 100, 60'000));

        owo::voice::VoiceMessage start_msg;
        start_msg.type = owo::voice::VoiceMessageType::start_request;
        start_msg.status = owo::voice::VoiceStatus::success;
        start_msg.request_id = ++request_id_;
        start_msg.timeout_ms = clamped_timeout;
        start_msg.plugin_id = std::string(owo::voice::kVoicePluginId);
        start_msg.language.assign(language);
        start_msg.grant = std::move(grant);

        const auto start_rid = start_msg.request_id;
        {
            std::lock_guard lock(mutex_);
            owner_.assign(owner);
            session_state_ = owo::ipc::VoiceSessionState::listening;
            session_text_.clear();
            session_diagnostic_.clear();
            session_request_id_ = start_rid;
        }

        {
            std::lock_guard lock(write_mutex_);
            if (!write_voice_message(start_msg))
                return fail_session("cannot send start request to voice module");
        }

        {
            std::unique_lock lock(mutex_);
            if (!ack_cv_.wait_for(lock, std::chrono::seconds(3),
                                  [this, start_rid] {
                                      return ack_received_ && ack_request_id_ == start_rid;
                                  })) {
                return fail_session("voice module did not acknowledge start request");
            }
            ack_received_ = false;
        }
        return {true, owo::ipc::VoiceSessionState::listening, {}, {}};
    }

    owo::ipc::VoiceSessionResult snapshot(const std::string_view owner) const {
        std::lock_guard lock(mutex_);
        if (owner_ != owner)
            return {false, owo::ipc::VoiceSessionState::idle, {},
                    "voice session owner mismatch"};
        return {true, session_state_, session_text_, session_diagnostic_};
    }

    owo::ipc::VoiceSessionResult cancel(const std::string_view owner) {
        {
            std::lock_guard lock(mutex_);
            if (owner_ != owner)
                return {false, owo::ipc::VoiceSessionState::idle, {},
                        "voice session owner mismatch"};
            if (session_state_ != owo::ipc::VoiceSessionState::listening)
                return {true, session_state_, session_text_, session_diagnostic_};
            session_state_ = owo::ipc::VoiceSessionState::cancelled;
            session_diagnostic_ = "voice input cancelled";
        }
        send_cancel_to_plugin();
        return {true, owo::ipc::VoiceSessionState::cancelled, {},
                "voice input cancelled"};
    }

    // --- Protocol I/O ---

    bool read_voice_message(owo::voice::VoiceMessage& message) {
        std::uint32_t size = 0;
        DWORD bytes_read = 0;
        if (!ReadFile(stdout_read_, &size, sizeof(size), &bytes_read, nullptr) ||
            bytes_read != sizeof(size)) return false;
        if (size == 0 || size > owo::protocol::kMaximumPayloadBytes) return false;
        std::string payload(size, '\0');
        DWORD total = 0;
        while (total < size) {
            if (!ReadFile(stdout_read_, payload.data() + total,
                          static_cast<DWORD>(size - total), &bytes_read, nullptr) ||
                bytes_read == 0) return false;
            total += bytes_read;
        }
        auto decoded = owo::voice::decode_voice_message(payload);
        if (!decoded.validation) return false;
        message = std::move(decoded.message);
        return true;
    }

    bool write_voice_message(const owo::voice::VoiceMessage& message) {
        auto payload = owo::voice::encode_voice_message(message);
        if (payload.empty()) return false;
        const std::uint32_t size = static_cast<std::uint32_t>(payload.size());
        DWORD written = 0;
        if (!WriteFile(stdin_write_, &size, sizeof(size), &written, nullptr) ||
            written != sizeof(size)) return false;
        if (!WriteFile(stdin_write_, payload.data(), size, &written, nullptr) ||
            written != size) return false;
        return true;
    }

    void send_cancel_to_plugin() {
        std::lock_guard lock(write_mutex_);
        owo::voice::VoiceMessage cancel_msg;
        cancel_msg.type = owo::voice::VoiceMessageType::cancel_request;
        cancel_msg.status = owo::voice::VoiceStatus::success;
        cancel_msg.request_id = ++request_id_;
        cancel_msg.plugin_id = std::string(owo::voice::kVoicePluginId);
        write_voice_message(cancel_msg);
    }

    void send_cancel_and_wait() {
        owo::voice::VoiceMessage cancel_msg;
        cancel_msg.type = owo::voice::VoiceMessageType::cancel_request;
        cancel_msg.status = owo::voice::VoiceStatus::success;
        cancel_msg.request_id = ++request_id_;
        cancel_msg.plugin_id = std::string(owo::voice::kVoicePluginId);
        const auto cancel_rid = cancel_msg.request_id;
        {
            std::lock_guard lock(write_mutex_);
            write_voice_message(cancel_msg);
        }
        {
            std::unique_lock lock(mutex_);
            ack_cv_.wait_for(lock, std::chrono::seconds(1),
                             [this, cancel_rid] {
                                 return ack_received_ && ack_request_id_ == cancel_rid;
                             });
            ack_received_ = false;
        }
        // Reset session state so the next start can proceed cleanly
        {
            std::lock_guard lock(mutex_);
            session_state_ = owo::ipc::VoiceSessionState::idle;
            session_text_.clear();
            session_diagnostic_.clear();
        }
    }

    owo::ipc::VoiceSessionResult fail_session(std::string diagnostic) {
        std::lock_guard lock(mutex_);
        session_state_ = owo::ipc::VoiceSessionState::failed;
        session_diagnostic_ = std::move(diagnostic);
        return {true, owo::ipc::VoiceSessionState::failed, {}, session_diagnostic_};
    }

    // --- Reader thread ---

    void reader_loop(const std::stop_token stop_token) {
        while (!stop_token.stop_requested()) {
            owo::voice::VoiceMessage message;
            if (!read_voice_message(message)) break;
            process_frame(message);
        }
        // Plugin exited or pipe broken
        std::lock_guard lock(mutex_);
        if (session_state_ == owo::ipc::VoiceSessionState::listening) {
            session_state_ = owo::ipc::VoiceSessionState::failed;
            session_diagnostic_ = "voice module exited unexpectedly";
        }
    }

    void process_frame(const owo::voice::VoiceMessage& message) {
        std::lock_guard lock(mutex_);

        if (message.type == owo::voice::VoiceMessageType::hello_request) {
            handshake_received_ = true;
            handshake_cv_.notify_one();
            return;
        }

        if (message.type == owo::voice::VoiceMessageType::acknowledgement) {
            ack_received_ = true;
            ack_request_id_ = message.request_id;
            ack_cv_.notify_one();
            return;
        }

        // Only process session frames for the active session
        const bool is_session_frame =
            message.type == owo::voice::VoiceMessageType::partial_result ||
            message.type == owo::voice::VoiceMessageType::final_result ||
            message.type == owo::voice::VoiceMessageType::listening_started ||
            message.type == owo::voice::VoiceMessageType::error_response;
        if (is_session_frame && message.request_id != session_request_id_) return;

        if (message.type == owo::voice::VoiceMessageType::partial_result) {
            session_text_ = message.text;
            return;
        }
        if (message.type == owo::voice::VoiceMessageType::listening_started) {
            return;
        }
        if (message.type == owo::voice::VoiceMessageType::final_result) {
            session_text_ = message.text;
            session_state_ = owo::ipc::VoiceSessionState::final_result;
            session_diagnostic_.clear();
            return;
        }
        if (message.type == owo::voice::VoiceMessageType::error_response) {
            session_state_ = owo::ipc::VoiceSessionState::failed;
            session_diagnostic_ = message.diagnostic;
            return;
        }
    }

    std::filesystem::path executable_;
    mutable std::mutex mutex_;
    std::mutex write_mutex_;

    HANDLE process_{nullptr};
    HANDLE stdin_write_{nullptr};
    HANDLE stdout_read_{nullptr};

    std::jthread reader_;
    bool handshake_complete_{false};
    bool handshake_received_{false};
    std::condition_variable handshake_cv_;

    std::uint64_t request_id_{1};
    std::uint64_t grant_counter_{0};
    std::string owner_;
    owo::ipc::VoiceSessionState session_state_{owo::ipc::VoiceSessionState::idle};
    std::string session_text_;
    std::string session_diagnostic_;
    std::uint64_t session_request_id_{0};

    std::condition_variable ack_cv_;
    bool ack_received_{false};
    std::uint64_t ack_request_id_{0};
};

}  // namespace

int wmain(int argc, wchar_t** argv) {
    std::cout << "OwO Core Service is ready.\n";
    const wchar_t* lexicon_path = nullptr;
    const wchar_t* user_frequency_path = nullptr;
    const wchar_t* model_pipe_name = nullptr;
    const wchar_t* config_path = nullptr;
    const wchar_t* plugin_store_path = nullptr;
    bool config_disabled = false;
    for (int index = 1; index < argc;) {
        const std::wstring_view option(argv[index]);
        if (option == L"--model-host") {
            model_pipe_name = owo::ipc::kModelHostPipeName;
            ++index;
        } else if (option == L"--no-config") {
            config_disabled = true;
            ++index;
        } else if (index + 1 >= argc) {
            return 2;
        } else if (option == L"--lexicon") {
            lexicon_path = argv[index + 1];
            index += 2;
        } else if (option == L"--user-frequency") {
            user_frequency_path = argv[index + 1];
            index += 2;
        } else if (option == L"--config") {
            config_path = argv[index + 1];
            index += 2;
        } else if (option == L"--plugin-store") {
            plugin_store_path = argv[index + 1];
            index += 2;
        }
        else return 2;
    }
    if (config_disabled && config_path != nullptr) return 2;
    const auto data_root = owo::config::local_data_root();
    const auto default_plugin_store = data_root.empty()
        ? std::filesystem::path{} : data_root / L"plugins";
    std::filesystem::path effective_plugin_store = plugin_store_path != nullptr
        ? std::filesystem::path(plugin_store_path) : default_plugin_store;
    effective_plugin_store = effective_plugin_store.lexically_normal();
    if (effective_plugin_store.empty()) {
        std::cerr << "default_plugin_store_unavailable\n";
        return 3;
    }
    if (!effective_plugin_store.is_absolute() ||
        effective_plugin_store.root_name().native().size() != 2 ||
        effective_plugin_store.root_name().native()[1] != L':' ||
        effective_plugin_store == effective_plugin_store.root_path()) {
        std::cerr << "plugin_store_must_be_local_absolute_child\n";
        return 2;
    }
    owo::core::PluginExecutor plugin_executor(std::move(effective_plugin_store));
    const owo::ipc::PluginCandidateProvider plugin_candidates =
        [&plugin_executor](const std::string_view query, const std::chrono::milliseconds timeout) {
            owo::core::PluginExecutionRequest request;
            request.plugin_id = "owo.plugin.emoji";
            request.service = "emoji.query.v1";
            request.payload.assign(query);
            request.source = owo::core::PluginCallSource::core_background;
            request.timeout = timeout;
            auto submission = plugin_executor.submit(std::move(request));
            if (submission.completion.wait_for(timeout) != std::future_status::ready)
                return owo::ipc::PluginCandidateResult{false, {}, "表情插件响应超时"};
            auto completed = submission.completion.get();
            if (!completed.ok)
                return owo::ipc::PluginCandidateResult{
                    false, {}, completed.diagnostic.empty()
                                   ? "表情插件调用失败" : std::move(completed.diagnostic)};
            owo::ipc::PluginCandidateResult result;
            result.ok = true;
            std::size_t offset = 0;
            while (offset <= completed.payload.size()) {
                const auto newline = completed.payload.find('\n', offset);
                const auto end = newline == std::string::npos ? completed.payload.size() : newline;
                auto candidate = completed.payload.substr(offset, end - offset);
                if (!candidate.empty()) result.candidates.push_back(std::move(candidate));
                if (newline == std::string::npos) break;
                offset = newline + 1;
            }
            return result;
        };
    std::clog << R"({"process":"core_service","module":"plugin","level":"info","event_id":"plugin_worker_ready"})"
              << '\n';
    VoiceProcessController voice_controller;
    const owo::ipc::VoiceSessionProvider voice_sessions =
        [&voice_controller](const owo::ipc::VoiceSessionCommand command,
                            const std::string_view owner,
                            const std::string_view language,
                            const std::chrono::milliseconds timeout) {
            return voice_controller.invoke(command, owner, language, timeout);
        };
    const auto default_config_path = owo::config::default_config_path();
    if (!config_disabled && config_path == nullptr) {
        if (default_config_path.empty()) {
            std::cerr << "default_config_path_unavailable\n";
            return 3;
        }
        config_path = default_config_path.c_str();
    }
    owo::config::ConfigMonitor config_monitor;
    const owo::config::ConfigMonitor* config_monitor_ptr = nullptr;
    if (config_path != nullptr) {
        const auto loaded = config_monitor.start(config_path);
        if (!loaded.success) {
            std::cerr << "config_start_failed: " << loaded.diagnostic << '\n';
            return 3;
        }
        if (!loaded.diagnostic.empty())
            std::clog << "config_diagnostic: " << loaded.diagnostic << '\n';
        config_monitor_ptr = &config_monitor;
    }
    owo::engine::UserFrequencyStore user_frequency;
    owo::engine::UserFrequencyStore* user_frequency_ptr = nullptr;
    if (user_frequency_path != nullptr) {
        const auto loaded = user_frequency.load(user_frequency_path);
        if (!loaded.success) {
            std::cerr << "user_frequency_load_failed: " << loaded.error << '\n';
            return 3;
        }
        user_frequency_ptr = &user_frequency;
    }
    if (lexicon_path != nullptr) {
        owo::engine::BinaryLexicon lexicon;
        const auto loaded = lexicon.load(lexicon_path);
        if (!loaded.success) {
            std::cerr << "lexicon_load_failed: " << loaded.error << '\n';
            return 3;
        }
        // Warm every dictionary page into the working set, then pin the working
        // set floor to the warmed size. Without this Windows trims the idle
        // process's pages to disk and the first keystroke after a pause stalls
        // for hundreds of ms on page faults (2026-08 latency probe).
        lexicon.touch_pages();
        {
            PROCESS_MEMORY_COUNTERS_EX mem{};
            mem.cb = sizeof(mem);
            if (GetProcessMemoryInfo(GetCurrentProcess(),
                                     reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&mem),
                                     sizeof(mem))) {
                SetProcessWorkingSetSize(GetCurrentProcess(), mem.WorkingSetSize, ~SIZE_T{0});
            }
        }
        return owo::ipc::run_core_server(owo::ipc::kCorePipeName, lexicon,
                                         user_frequency_ptr, model_pipe_name,
                                         config_monitor_ptr, &plugin_candidates, &voice_sessions);
    }
    if (user_frequency_ptr != nullptr) {
        const owo::engine::MemoryLexicon fallback({
            {{"ni", "hao"}, "你好", 1000}, {{"ni", "hao"}, "你号", 50},
            {{"xian"}, "先", 800}, {{"xian"}, "线", 700}, {{"xi", "an"}, "西安", 900}});
        return owo::ipc::run_core_server(owo::ipc::kCorePipeName, fallback,
                                         user_frequency_ptr, model_pipe_name,
                                         config_monitor_ptr, &plugin_candidates, &voice_sessions);
    }
    if (model_pipe_name != nullptr) {
        const owo::engine::MemoryLexicon fallback({
            {{"ni", "hao"}, "你好", 1000}, {{"ni", "hao"}, "你号", 50},
            {{"xian"}, "先", 800}, {{"xian"}, "线", 700}, {{"xi", "an"}, "西安", 900}});
        return owo::ipc::run_core_server(owo::ipc::kCorePipeName, fallback, nullptr,
                                         model_pipe_name, config_monitor_ptr,
                                         &plugin_candidates, &voice_sessions);
    }
    if (config_monitor_ptr != nullptr) {
        const owo::engine::MemoryLexicon fallback({
            {{"ni", "hao"}, "你好", 1000}, {{"ni", "hao"}, "你号", 50},
            {{"xian"}, "先", 800}, {{"xian"}, "线", 700}, {{"xi", "an"}, "西安", 900}});
        return owo::ipc::run_core_server(owo::ipc::kCorePipeName, fallback, nullptr,
                                         nullptr, config_monitor_ptr,
                                         &plugin_candidates, &voice_sessions);
    }
    return owo::ipc::run_core_server(owo::ipc::kCorePipeName);
}
