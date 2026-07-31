#include "owo/ipc/named_pipe.h"
#include "owo/engine/binary_lexicon.h"
#include "owo/protocol/messages.h"

#include <chrono>
#include <atomic>
#include <iostream>
#include <filesystem>
#include <thread>

namespace {

constexpr wchar_t kContractPipe[] = LR"(\\.\pipe\OwO.InputMethod.ContractTest)";

owo::protocol::DecodeResult exchange(const owo::protocol::Message& request) {
    for (int attempt = 0; attempt < 100; ++attempt) {
        const auto result = owo::ipc::exchange(kContractPipe,
                                               owo::protocol::encode_message(request),
                                               std::chrono::milliseconds(100));
        if (result.status) return owo::protocol::decode_message(result.response);
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    return {};
}

bool valid_response(const owo::protocol::DecodeResult& response,
                    const std::uint64_t request_id,
                    const std::uint64_t generation) {
    return response.validation &&
           response.message.type == owo::protocol::MessageType::candidate_response &&
           response.message.request_id == request_id &&
           response.message.context_generation == generation &&
           response.message.candidates == std::vector<std::string>{"你好", "你号"};
}

}  // namespace

int main() {
    const auto path = std::filesystem::temp_directory_path() / "owo-core-contract.owolx";
    const auto written = owo::engine::write_binary_lexicon(path, {
        {{"ni", "hao"}, "你好", 1000}, {{"ni", "hao"}, "你号", 50}});
    owo::engine::BinaryLexicon lexicon;
    const auto loaded = lexicon.load(path);
    if (!written.success || !loaded.success) return 2;
    std::atomic<int> server_exit{-1};
    std::jthread server([&server_exit, &lexicon] {
        server_exit = owo::ipc::run_core_server(kContractPipe, lexicon);
    });
    const auto first = exchange({owo::protocol::MessageType::candidate_request, 101, 7, "nihao"});
    const auto second = exchange({owo::protocol::MessageType::candidate_request, 102, 8, "nihao"});
    const auto shutdown = exchange({owo::protocol::MessageType::shutdown_request, 103, 9, {}});
    server.join();
    std::error_code ignored;
    std::filesystem::remove(path, ignored);
    if (!valid_response(first, 101, 7) || !valid_response(second, 102, 8) ||
        !shutdown.validation || shutdown.message.text != "shutdown_ack" ||
        shutdown.message.request_id != 103 || shutdown.message.context_generation != 9 ||
        server_exit != 0) {
        std::cerr << "in-process core service contract failed\n";
        return 1;
    }
    return 0;
}
