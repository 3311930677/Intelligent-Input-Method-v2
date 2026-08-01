#include "owo/ipc/named_pipe.h"
#include "owo/model/model_backend.h"

#include <charconv>
#include <chrono>
#include <string_view>

int main(int argc, char** argv) {
    owo::model::MockBackendOptions options;
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
        } else {
            return 2;
        }
    }
    owo::model::MockModelBackend backend(options);
    return owo::ipc::run_model_server(owo::ipc::kModelHostPipeName, backend);
}
