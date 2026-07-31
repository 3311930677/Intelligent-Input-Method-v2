#include "owo/ipc/named_pipe.h"
#include "owo/protocol/messages.h"

#include <chrono>
#include <iostream>
#include <string>
#include <string_view>

int main(int argc, char** argv) {
    const bool shutdown = argc > 1 && std::string_view(argv[1]) == "--shutdown";
    const std::string input = argc > 1 && !shutdown ? argv[1] : "test";
    const owo::protocol::Message request{
        shutdown ? owo::protocol::MessageType::shutdown_request
                 : owo::protocol::MessageType::candidate_request,
        1, 1, input};
    const auto result = owo::ipc::exchange(
        owo::ipc::kCorePipeName, owo::protocol::encode_message(request),
        std::chrono::milliseconds(2000));
    if (!result.status) {
        std::cerr << result.status.message << '\n';
        return 2;
    }
    const auto decoded = owo::protocol::decode_message(result.response);
    const auto expected_type = shutdown ? owo::protocol::MessageType::acknowledgement
                                        : owo::protocol::MessageType::candidate_response;
    if (!decoded.validation || decoded.message.type != expected_type ||
        decoded.message.request_id != request.request_id ||
        decoded.message.context_generation != request.context_generation) {
        std::cerr << "invalid or stale response\n";
        return 3;
    }
    if (shutdown) {
        std::cout << decoded.message.text << '\n';
    } else {
        for (std::size_t index = 0; index < decoded.message.candidates.size(); ++index) {
            std::cout << index + 1 << ". " << decoded.message.candidates[index] << '\n';
        }
    }
    return 0;
}
