#include "owo/ipc/named_pipe.h"
#include "owo/engine/binary_lexicon.h"
#include "owo/engine/user_frequency.h"
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
                    const std::uint64_t generation,
                    const std::vector<std::string>& candidates = {"你好", "你号"}) {
    return response.validation &&
           response.message.type == owo::protocol::MessageType::candidate_response &&
           response.message.request_id == request_id &&
           response.message.context_generation == generation &&
           response.message.candidates == candidates;
}

}  // namespace

int main() {
    const auto path = std::filesystem::temp_directory_path() / "owo-core-contract.owolx";
    const auto user_path = std::filesystem::temp_directory_path() / "owo-core-contract-user.bin";
    std::error_code ignored;
    std::filesystem::remove(user_path, ignored);
    std::filesystem::remove(user_path.wstring() + L".bak", ignored);
    const auto written = owo::engine::write_binary_lexicon(path, {
        {{"ni", "hao"}, "你好", 1000}, {{"ni", "hao"}, "你号", 50},
        {{"ce", "shi"}, "测试一", 1000}, {{"ce", "shi"}, "测试二", 900},
        {{"ce", "shi"}, "测试三", 800}, {{"ce", "shi"}, "测试四", 700},
        {{"ce", "shi"}, "测试五", 600}, {{"ce", "shi"}, "测试六", 500},
        {{"ce", "shi"}, "测试七", 400}});
    owo::engine::BinaryLexicon lexicon;
    const auto loaded = lexicon.load(path);
    owo::engine::UserFrequencyStore user_frequency;
    if (!written.success || !loaded.success || !user_frequency.load(user_path).success) return 2;
    std::atomic<int> server_exit{-1};
    std::jthread server([&server_exit, &lexicon, &user_frequency] {
        server_exit = owo::ipc::run_core_server(kContractPipe, lexicon, &user_frequency);
    });
    const auto first = exchange({owo::protocol::MessageType::candidate_request, 101, 7, "nihao"});
    const auto second = exchange({owo::protocol::MessageType::candidate_request, 102, 8, "nihao"});
    bool commits_ok = true;
    for (std::uint64_t request = 0; request < 5; ++request) {
        const auto committed = exchange({owo::protocol::MessageType::candidate_committed,
                                         200 + request, 8, "你号"});
        commits_ok = commits_ok && committed.validation &&
                     committed.message.type == owo::protocol::MessageType::acknowledgement;
    }
    const auto learned = exchange({owo::protocol::MessageType::candidate_request, 104, 8, "nihao"});
    const auto first_page = exchange({owo::protocol::MessageType::candidate_request, 106, 8, "ceshi"});
    owo::protocol::Message second_page_request{
        owo::protocol::MessageType::candidate_request, 105, 8, "ceshi"};
    second_page_request.page = 1;
    const auto second_page = exchange(second_page_request);
    const auto shutdown = exchange({owo::protocol::MessageType::shutdown_request, 103, 9, {}});
    server.join();
    std::filesystem::remove(path, ignored);
    owo::engine::UserFrequencyStore persisted;
    const auto persisted_result = persisted.load(user_path);
    std::filesystem::remove(user_path, ignored);
    std::filesystem::remove(user_path.wstring() + L".bak", ignored);
    if (!commits_ok || !valid_response(first, 101, 7) || !valid_response(second, 102, 8) ||
        !valid_response(learned, 104, 8, {"你号", "你好"}) ||
        !valid_response(first_page, 106, 8,
                        {"测试一", "测试二", "测试三", "测试四", "测试五"}) ||
        first_page.message.page != 0 || !first_page.message.has_more ||
        !valid_response(second_page, 105, 8, {"测试六", "测试七"}) ||
        second_page.message.page != 1 || second_page.message.has_more ||
        !persisted_result.success || persisted.count("你号") != 5 ||
        !shutdown.validation || shutdown.message.type != owo::protocol::MessageType::acknowledgement ||
        shutdown.message.text != "shutdown_ack" ||
        shutdown.message.request_id != 103 || shutdown.message.context_generation != 9 ||
        server_exit != 0) {
        std::cerr << "in-process core service contract failed\n";
        return 1;
    }
    return 0;
}
