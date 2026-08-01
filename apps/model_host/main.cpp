#include "owo/ipc/named_pipe.h"
#include "owo/model/model_backend.h"
#include "owo/model/model_assets.h"

#include <charconv>
#include <chrono>
#include <iostream>
#include <string>
#include <string_view>

int main(int argc, char** argv) {
    owo::model::MockBackendOptions options;
    std::string asset_manifest;
    for (int index = 1; index < argc; ++index) {
        const std::string_view argument(argv[index]);
        if (argument == "--fail") {
            options.fail = true;
        } else if (argument == "--latency-ms" && index + 1 < argc) {
            const std::string_view value(argv[++index]);
            std::uint64_t milliseconds{};
            const auto parsed = std::from_chars(value.data(), value.data() + value.size(), milliseconds);
            if (parsed.ec != std::errc{} || parsed.ptr != value.data() + value.size() ||
                milliseconds > 60'000) return 2;
            options.latency = std::chrono::milliseconds(milliseconds);
        } else if (argument == "--asset-manifest" && index + 1 < argc) {
            asset_manifest = argv[++index];
        } else {
            return 2;
        }
    }
    if (!asset_manifest.empty()) {
        const auto loaded = owo::model::load_model_assets(asset_manifest);
        if (!loaded.ok) {
            std::cerr << "model asset validation failed: " << loaded.diagnostic << '\n';
            return 3;
        }
        std::cerr << "model assets validated; inference backend remains mock\n";
    }
    owo::model::MockModelBackend backend(options);
    return owo::ipc::run_model_server(owo::ipc::kModelHostPipeName, backend);
}
