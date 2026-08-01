#include "owo/ipc/named_pipe.h"
#include "owo/model/model_backend.h"
#include "owo/model/model_assets.h"
#include "owo/model/model_inference.h"
#ifdef OWO_HAS_ONNXRUNTIME
#include "owo/model/onnxruntime_session.h"
#endif

#include <charconv>
#include <chrono>
#include <iostream>
#include <memory>
#include <string>
#include <string_view>

int main(int argc, char** argv) {
    owo::model::MockBackendOptions options;
    std::string asset_manifest;
    bool synthetic_session = false;
    bool onnxruntime_session = false;
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
        } else if (argument == "--synthetic-session") {
            synthetic_session = true;
        } else if (argument == "--onnxruntime-session") {
            onnxruntime_session = true;
        } else {
            return 2;
        }
    }
    if ((synthetic_session && onnxruntime_session) ||
        ((synthetic_session || onnxruntime_session) && asset_manifest.empty())) return 2;
    std::unique_ptr<owo::model::IModelBackend> backend;
    if (!asset_manifest.empty()) {
        auto loaded = owo::model::load_model_assets(asset_manifest);
        if (!loaded.ok) {
            std::cerr << "model asset validation failed: " << loaded.diagnostic << '\n';
            return 3;
        }
        if (onnxruntime_session) {
#ifdef OWO_HAS_ONNXRUNTIME
            const auto created = owo::model::create_onnxruntime_cpu_session(
                loaded.value.manifest, loaded.value.model_path);
            if (!created) {
                std::cerr << "ONNX Runtime session creation failed: " << created.diagnostic << '\n';
                return 4;
            }
            backend = std::make_unique<owo::model::AssetCandidateRanker>(
                std::move(loaded.value.manifest), std::move(loaded.value.vocabulary),
                created.session);
            std::cerr << "model assets validated; ONNX Runtime CPU session enabled\n";
#else
            std::cerr << "ONNX Runtime support is not compiled in\n";
            return 4;
#endif
        } else if (synthetic_session) {
            auto session = std::make_shared<owo::model::SyntheticInferenceSession>();
            backend = std::make_unique<owo::model::AssetCandidateRanker>(
                std::move(loaded.value.manifest), std::move(loaded.value.vocabulary),
                std::move(session));
            std::cerr << "model assets validated; synthetic inference session enabled\n";
        } else {
            std::cerr << "model assets validated; inference backend remains mock\n";
        }
    }
    if (!backend) backend = std::make_unique<owo::model::MockModelBackend>(options);
    return owo::ipc::run_model_server(owo::ipc::kModelHostPipeName, *backend);
}
