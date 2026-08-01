#include "owo/ipc/named_pipe.h"
#include "owo/config/config_monitor.h"
#include "owo/config/config_paths.h"
#include "owo/engine/binary_lexicon.h"
#include "owo/engine/user_frequency.h"

#include <iostream>
#include <string_view>

int wmain(int argc, wchar_t** argv) {
    std::cout << "OwO Core Service P2 prototype is ready.\n";
    const wchar_t* lexicon_path = nullptr;
    const wchar_t* user_frequency_path = nullptr;
    const wchar_t* model_pipe_name = nullptr;
    const wchar_t* config_path = nullptr;
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
        }
        else return 2;
    }
    if (config_disabled && config_path != nullptr) return 2;
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
                                         config_monitor_ptr);
    }
    if (user_frequency_ptr != nullptr) {
        const owo::engine::MemoryLexicon fallback({
            {{"ni", "hao"}, "你好", 1000}, {{"ni", "hao"}, "你号", 50},
            {{"xian"}, "先", 800}, {{"xian"}, "线", 700}, {{"xi", "an"}, "西安", 900}});
        return owo::ipc::run_core_server(owo::ipc::kCorePipeName, fallback,
                                         user_frequency_ptr, model_pipe_name,
                                         config_monitor_ptr);
    }
    if (model_pipe_name != nullptr) {
        const owo::engine::MemoryLexicon fallback({
            {{"ni", "hao"}, "你好", 1000}, {{"ni", "hao"}, "你号", 50},
            {{"xian"}, "先", 800}, {{"xian"}, "线", 700}, {{"xi", "an"}, "西安", 900}});
        return owo::ipc::run_core_server(owo::ipc::kCorePipeName, fallback, nullptr,
                                         model_pipe_name, config_monitor_ptr);
    }
    if (config_monitor_ptr != nullptr) {
        const owo::engine::MemoryLexicon fallback({
            {{"ni", "hao"}, "你好", 1000}, {{"ni", "hao"}, "你号", 50},
            {{"xian"}, "先", 800}, {{"xian"}, "线", 700}, {{"xi", "an"}, "西安", 900}});
        return owo::ipc::run_core_server(owo::ipc::kCorePipeName, fallback, nullptr,
                                         nullptr, config_monitor_ptr);
    }
    return owo::ipc::run_core_server(owo::ipc::kCorePipeName);
}
