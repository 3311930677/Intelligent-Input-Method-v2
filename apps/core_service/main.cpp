#include "owo/ipc/named_pipe.h"
#include "owo/engine/binary_lexicon.h"

#include <iostream>
#include <string_view>

int wmain(int argc, wchar_t** argv) {
    std::cout << "OwO Core Service P2 prototype is ready.\n";
    if (argc == 3 && std::wstring_view(argv[1]) == L"--lexicon") {
        owo::engine::BinaryLexicon lexicon;
        const auto loaded = lexicon.load(argv[2]);
        if (!loaded.success) {
            std::cerr << "lexicon_load_failed: " << loaded.error << '\n';
            return 3;
        }
        return owo::ipc::run_core_server(owo::ipc::kCorePipeName, lexicon);
    }
    if (argc != 1) {
        std::cerr << "usage: owo_core_service [--lexicon <path>]\n";
        return 2;
    }
    return owo::ipc::run_core_server(owo::ipc::kCorePipeName);
}
