#include "owo/ipc/named_pipe.h"
#include "owo/engine/binary_lexicon.h"
#include "owo/engine/user_frequency.h"

#include <iostream>
#include <string_view>

int wmain(int argc, wchar_t** argv) {
    std::cout << "OwO Core Service P2 prototype is ready.\n";
    const wchar_t* lexicon_path = nullptr;
    const wchar_t* user_frequency_path = nullptr;
    const wchar_t* model_pipe_name = nullptr;
    for (int index = 1; index < argc;) {
        const std::wstring_view option(argv[index]);
        if (option == L"--model-host") {
            model_pipe_name = owo::ipc::kModelHostPipeName;
            ++index;
        } else if (index + 1 >= argc) {
            return 2;
        } else if (option == L"--lexicon") {
            lexicon_path = argv[index + 1];
            index += 2;
        } else if (option == L"--user-frequency") {
            user_frequency_path = argv[index + 1];
            index += 2;
        }
        else return 2;
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
                                         user_frequency_ptr, model_pipe_name);
    }
    if (user_frequency_ptr != nullptr) {
        const owo::engine::MemoryLexicon fallback({
            {{"ni", "hao"}, "你好", 1000}, {{"ni", "hao"}, "你号", 50},
            {{"xian"}, "先", 800}, {{"xian"}, "线", 700}, {{"xi", "an"}, "西安", 900}});
        return owo::ipc::run_core_server(owo::ipc::kCorePipeName, fallback,
                                         user_frequency_ptr, model_pipe_name);
    }
    if (model_pipe_name != nullptr) {
        const owo::engine::MemoryLexicon fallback({
            {{"ni", "hao"}, "你好", 1000}, {{"ni", "hao"}, "你号", 50},
            {{"xian"}, "先", 800}, {{"xian"}, "线", 700}, {{"xi", "an"}, "西安", 900}});
        return owo::ipc::run_core_server(owo::ipc::kCorePipeName, fallback, nullptr,
                                         model_pipe_name);
    }
    return owo::ipc::run_core_server(owo::ipc::kCorePipeName);
}
