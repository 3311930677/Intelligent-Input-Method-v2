#include "owo/ipc/named_pipe.h"
#include "owo/config/config_monitor.h"
#include "owo/config/config_paths.h"
#include "owo/core/plugin_executor.h"
#include "owo/engine/binary_lexicon.h"
#include "owo/engine/user_frequency.h"

#include <Windows.h>

#include <algorithm>
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

std::string trim_line(std::string line) {
    while (!line.empty() && (line.back() == '\r' || line.back() == '\n')) line.pop_back();
    return line;
}

class VoiceProcessController final {
public:
    VoiceProcessController()
        : executable_(sibling_executable(L"owo_voice_input_plugin.exe")) {}

    ~VoiceProcessController() {
        HANDLE process = nullptr;
        {
            std::lock_guard lock(mutex_);
            process = process_;
            state_ = owo::ipc::VoiceSessionState::cancelled;
        }
        if (process != nullptr) TerminateProcess(process, ERROR_CANCELLED);
        if (worker_.joinable()) worker_.join();
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
    owo::ipc::VoiceSessionResult start(
        const std::string_view owner, const std::string_view language,
        const std::chrono::milliseconds timeout) {
        if (executable_.empty() || !std::filesystem::is_regular_file(executable_))
            return {false, owo::ipc::VoiceSessionState::idle, {},
                    "voice module executable is missing"};
        std::jthread previous;
        {
    std::lock_guard lock(mutex_);
            if (state_ == owo::ipc::VoiceSessionState::listening) {
      // Preempt the previous session rather than rejecting the new
      // owner. A fresh start (user pressing F9 / 重试 again) should
       // take over: terminate the old process so its worker exits and
                // let the new owner claim the session. Rejecting here would
                // leave owner_ pointing at the stale session, so the new owner
      // would poll and hit "voice session owner mismatch".
      state_ = owo::ipc::VoiceSessionState::cancelled;
      if (process_ != nullptr) {
     TerminateProcess(process_, ERROR_CANCELLED);
   process_ = nullptr;
    }
    }
       previous = std::move(worker_);
        }
        if (previous.joinable()) previous.join();

        std::uint64_t generation{};
        {
            std::lock_guard lock(mutex_);
            owner_.assign(owner);
            language_.assign(language);
            text_.clear();
            diagnostic_.clear();
            state_ = owo::ipc::VoiceSessionState::listening;
            generation = ++generation_;
            worker_ = std::jthread([this, generation, timeout] {
                run_session(generation, timeout);
            });
        }
        return {true, owo::ipc::VoiceSessionState::listening, {}, {}};
    }

    owo::ipc::VoiceSessionResult snapshot(const std::string_view owner) const {
        std::lock_guard lock(mutex_);
        if (owner_ != owner)
            return {false, owo::ipc::VoiceSessionState::idle, {},
                    "voice session owner mismatch"};
        return {true, state_, text_, diagnostic_};
    }

    owo::ipc::VoiceSessionResult cancel(const std::string_view owner) {
        HANDLE process = nullptr;
        {
            std::lock_guard lock(mutex_);
            if (owner_ != owner)
                return {false, owo::ipc::VoiceSessionState::idle, {},
                        "voice session owner mismatch"};
            if (state_ != owo::ipc::VoiceSessionState::listening)
                return {true, state_, text_, diagnostic_};
            state_ = owo::ipc::VoiceSessionState::cancelled;
            diagnostic_ = "voice input cancelled";
            process = process_;
        }
        if (process != nullptr) TerminateProcess(process, ERROR_CANCELLED);
        return {true, owo::ipc::VoiceSessionState::cancelled, {},
                "voice input cancelled"};
    }

    static void read_available(const HANDLE pipe, std::string& buffer) {
        for (;;) {
            DWORD available = 0;
            if (!PeekNamedPipe(pipe, nullptr, 0, nullptr, &available, nullptr) || available == 0)
                return;
            const auto chunk_size = static_cast<std::size_t>(
                (std::min)(available, static_cast<DWORD>(4096)));
            std::string chunk(chunk_size, '\0');
            DWORD read = 0;
            if (!ReadFile(pipe, chunk.data(), static_cast<DWORD>(chunk.size()), &read, nullptr) ||
                read == 0) return;
            chunk.resize(read);
            buffer += chunk;
        }
    }

    template <typename Callback>
    static void consume_lines(std::string& buffer, Callback&& callback, const bool flush) {
        for (;;) {
            const auto newline = buffer.find('\n');
            if (newline == std::string::npos) break;
            auto line = trim_line(buffer.substr(0, newline + 1));
            buffer.erase(0, newline + 1);
            if (!line.empty()) callback(std::move(line));
        }
        if (flush && !buffer.empty()) {
            auto line = trim_line(std::move(buffer));
            buffer.clear();
            if (!line.empty()) callback(std::move(line));
        }
    }

    void run_session(const std::uint64_t generation,
                     const std::chrono::milliseconds timeout) {
        SECURITY_ATTRIBUTES attributes{sizeof(attributes), nullptr, TRUE};
        HANDLE stdout_read = nullptr;
        HANDLE stdout_write = nullptr;
        HANDLE stderr_read = nullptr;
        HANDLE stderr_write = nullptr;
        if (!CreatePipe(&stdout_read, &stdout_write, &attributes, 0) ||
            !CreatePipe(&stderr_read, &stderr_write, &attributes, 0)) {
            fail(generation, "cannot create voice process pipes");
            if (stdout_read) CloseHandle(stdout_read);
            if (stdout_write) CloseHandle(stdout_write);
            if (stderr_read) CloseHandle(stderr_read);
            if (stderr_write) CloseHandle(stderr_write);
            return;
        }
        SetHandleInformation(stdout_read, HANDLE_FLAG_INHERIT, 0);
        SetHandleInformation(stderr_read, HANDLE_FLAG_INHERIT, 0);
        HANDLE null_input = CreateFileW(L"NUL", GENERIC_READ,
                                        FILE_SHARE_READ | FILE_SHARE_WRITE,
                                        &attributes, OPEN_EXISTING, 0, nullptr);
        if (null_input == INVALID_HANDLE_VALUE) {
            fail(generation, "cannot open voice process input");
            CloseHandle(stdout_read);
            CloseHandle(stdout_write);
            CloseHandle(stderr_read);
            CloseHandle(stderr_write);
            return;
        }

        std::string language;
        {
            std::lock_guard lock(mutex_);
            if (generation != generation_) {
                CloseHandle(null_input);
                CloseHandle(stdout_read);
                CloseHandle(stdout_write);
                CloseHandle(stderr_read);
                CloseHandle(stderr_write);
                return;
            }
            language = language_;
        }
        std::wstring language_wide(language.begin(), language.end());
        std::wstring command = L"\"" + executable_.wstring() +
            L"\" --once --language " + language_wide + L" --timeout-ms " +
            std::to_wstring(timeout.count());
        STARTUPINFOW startup{sizeof(startup)};
        startup.dwFlags = STARTF_USESTDHANDLES;
        startup.hStdInput = null_input;
        startup.hStdOutput = stdout_write;
        startup.hStdError = stderr_write;
        PROCESS_INFORMATION process{};
        const BOOL created = CreateProcessW(executable_.c_str(), command.data(), nullptr, nullptr,
                                            TRUE, CREATE_NO_WINDOW, nullptr,
                                            executable_.parent_path().c_str(),
                                            &startup, &process);
        CloseHandle(null_input);
        CloseHandle(stdout_write);
        CloseHandle(stderr_write);
        if (!created) {
            CloseHandle(stdout_read);
            CloseHandle(stderr_read);
            fail(generation, "cannot start voice module: Win32 " +
                             std::to_string(GetLastError()));
            return;
        }
        CloseHandle(process.hThread);
        {
            std::lock_guard lock(mutex_);
            if (generation == generation_) process_ = process.hProcess;
        }

        std::string stdout_buffer;
        std::string stderr_buffer;
        std::string final_text;
        std::string last_diagnostic;
        const auto consume_stdout = [&](std::string line) {
            final_text = std::move(line);
        };
        const auto consume_stderr = [&](std::string line) {
            constexpr std::string_view prefix = "partial: ";
            if (line.starts_with(prefix)) {
                auto partial = line.substr(prefix.size());
                std::lock_guard lock(mutex_);
                if (generation == generation_ &&
                    state_ == owo::ipc::VoiceSessionState::listening)
                    text_ = std::move(partial);
            } else {
                last_diagnostic = std::move(line);
            }
        };

        while (WaitForSingleObject(process.hProcess, 25) == WAIT_TIMEOUT) {
            read_available(stdout_read, stdout_buffer);
            read_available(stderr_read, stderr_buffer);
            consume_lines(stdout_buffer, consume_stdout, false);
            consume_lines(stderr_buffer, consume_stderr, false);
        }
        read_available(stdout_read, stdout_buffer);
        read_available(stderr_read, stderr_buffer);
        consume_lines(stdout_buffer, consume_stdout, true);
        consume_lines(stderr_buffer, consume_stderr, true);
        DWORD exit_code = 1;
        GetExitCodeProcess(process.hProcess, &exit_code);
        CloseHandle(stdout_read);
        CloseHandle(stderr_read);

        {
            std::lock_guard lock(mutex_);
            if (generation != generation_) {
                CloseHandle(process.hProcess);
                return;
            }
            process_ = nullptr;
            if (state_ == owo::ipc::VoiceSessionState::cancelled) {
                CloseHandle(process.hProcess);
                return;
            }
            if (exit_code == 0 && !final_text.empty()) {
                state_ = owo::ipc::VoiceSessionState::final_result;
                text_ = std::move(final_text);
                diagnostic_.clear();
            } else {
                state_ = owo::ipc::VoiceSessionState::failed;
                diagnostic_ = last_diagnostic.empty()
                                  ? "voice recognition failed with exit code " +
                                        std::to_string(exit_code)
                                  : std::move(last_diagnostic);
            }
        }
        CloseHandle(process.hProcess);
    }

    void fail(const std::uint64_t generation, std::string diagnostic) {
        std::lock_guard lock(mutex_);
        if (generation != generation_) return;
        state_ = owo::ipc::VoiceSessionState::failed;
        diagnostic_ = std::move(diagnostic);
        process_ = nullptr;
    }

    std::filesystem::path executable_;
    mutable std::mutex mutex_;
    std::jthread worker_;
    HANDLE process_{nullptr};
    std::uint64_t generation_{};
    std::string owner_;
    std::string language_;
    owo::ipc::VoiceSessionState state_{owo::ipc::VoiceSessionState::idle};
    std::string text_;
    std::string diagnostic_;
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
