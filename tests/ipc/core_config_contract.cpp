#include "owo/config/config_monitor.h"
#include "owo/config/config_store.h"
#include "owo/engine/lexicon.h"
#include "owo/engine/user_frequency.h"
#include "owo/ipc/named_pipe.h"
#include "owo/protocol/messages.h"

#include <chrono>
#include <filesystem>
#include <iostream>
#include <thread>

namespace {

owo::protocol::DecodeResult send(const std::wstring& pipe, const owo::protocol::Message& message) {
    for (int attempt = 0; attempt < 100; ++attempt) {
        const auto exchanged = owo::ipc::exchange(
            pipe.c_str(), owo::protocol::encode_message(message), std::chrono::milliseconds(100));
        if (exchanged.status) return owo::protocol::decode_message(exchanged.response);
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    return {};
}

bool acknowledged(const owo::protocol::DecodeResult& result, std::string_view text) {
    return result.validation &&
           result.message.type == owo::protocol::MessageType::acknowledgement &&
           result.message.text == text;
}

}  // namespace

int wmain(int argc, wchar_t** argv) {
    if (argc != 2) return 2;
    const std::filesystem::path root(argv[1]);
    std::error_code ignored;
    std::filesystem::remove_all(root, ignored);
    std::filesystem::create_directories(root, ignored);
    const auto config_path = root / "owo.conf";
    const auto frequency_path = root / "user.bin";

    owo::config::ConfigStore writer;
    if (!writer.load(config_path).success) return 2;
    auto config = writer.snapshot();
    config.user_learning_enabled = false;
    if (!writer.save(config).success) return 2;

    owo::config::ConfigMonitor monitor;
    if (!monitor.start(config_path, std::chrono::milliseconds(10)).success) return 2;
    owo::engine::UserFrequencyStore frequencies;
    if (!frequencies.load(frequency_path).success) return 2;
    const owo::engine::MemoryLexicon lexicon({{{"ni", "hao"}, "你好", 1000}});
    const auto suffix = std::chrono::steady_clock::now().time_since_epoch().count();
    const auto pipe = LR"(\\.\pipe\OwO.InputMethod.ConfigContract.)" + std::to_wstring(suffix);
    const auto model_pipe = LR"(\\.\pipe\OwO.InputMethod.ConfigModelContract.)" +
                            std::to_wstring(suffix);

    int server_exit = -1;
    std::jthread server([&] {
        server_exit = owo::ipc::run_core_server(pipe.c_str(), lexicon, &frequencies,
                                                model_pipe.c_str(), &monitor);
    });
    const auto model_disabled = send(pipe, {owo::protocol::MessageType::candidate_request,
                                            10, 1, "nihao"});
    const auto disabled = send(pipe, {owo::protocol::MessageType::candidate_committed,
                                      1, 1, "你好"});
    const auto generation = monitor.generation();
    config.user_learning_enabled = true;
    config.model_ranking_enabled = true;
    config.model_timeout_ms = 5;
    if (!writer.save(config).success ||
        !monitor.wait_for_generation(generation, std::chrono::seconds(2))) return 2;
    const auto enabled = send(pipe, {owo::protocol::MessageType::candidate_committed,
                                     2, 1, "你好"});
    const auto model_enabled = send(pipe, {owo::protocol::MessageType::candidate_request,
                                           11, 1, "nihao"});
    const auto generation_after_enable = monitor.generation();
    config.model_ranking_enabled = false;
    if (!writer.save(config).success ||
        !monitor.wait_for_generation(generation_after_enable, std::chrono::seconds(2))) return 2;
    const auto model_disabled_again = send(
        pipe, {owo::protocol::MessageType::candidate_request, 12, 1, "nihao"});
    const auto shutdown = send(pipe, {owo::protocol::MessageType::shutdown_request,
                                      3, 1, {}});
    server.join();

    owo::engine::UserFrequencyStore persisted;
    const auto loaded = persisted.load(frequency_path);
    const bool ok = acknowledged(disabled, "commit_ack") &&
                    acknowledged(enabled, "commit_ack") &&
                    acknowledged(shutdown, "shutdown_ack") && server_exit == 0 &&
                    model_disabled.validation && !model_disabled.message.model_pending &&
                    model_enabled.validation && model_enabled.message.model_pending &&
                    model_disabled_again.validation &&
                    !model_disabled_again.message.model_pending &&
                    loaded.success && persisted.count("你好") == 1;
    std::filesystem::remove_all(root, ignored);
    if (!ok) {
        std::cerr << "core hot configuration contract failed\n";
        return 1;
    }
    return 0;
}
