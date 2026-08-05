#include "owo/ipc/named_pipe.h"
#include "owo/protocol/messages.h"

#include <chrono>
#include <iostream>
#include <string>
#include <string_view>
#include <thread>

namespace {

owo::protocol::DecodeResult voice_exchange(const owo::protocol::MessageType type,
                                           const std::uint64_t request_id,
                                           std::string text,
                                           const std::uint64_t timeout_ms,
                                           const std::chrono::milliseconds wait) {
    owo::protocol::Message request;
    request.type = type;
    request.request_id = request_id;
    request.context_generation = 1;
    request.text = std::move(text);
    request.page = timeout_ms;
    const auto result = owo::ipc::exchange(
        owo::ipc::kCorePipeName, owo::protocol::encode_message(request), wait);
    if (!result.status) {
        owo::protocol::DecodeResult failed;
        failed.validation = result.status;
        return failed;
    }
    return owo::protocol::decode_message(result.response);
}

std::string voice_state(const owo::protocol::DecodeResult& decoded) {
    if (!decoded.validation) return "(no response: " + decoded.validation.message + ")";
    if (decoded.message.type == owo::protocol::MessageType::error_response)
        return "error: " + decoded.message.text;
    if (decoded.message.candidates.empty()) return "(voice_response without state)";
    std::string label = decoded.message.candidates.front();
    if (decoded.message.candidates.size() > 1)
        label += " | diag=" + decoded.message.candidates[1];
    if (!decoded.message.text.empty()) label += " | text=" + decoded.message.text;
    return label;
}

// Exercises the Core voice path headlessly: start a session, poll until the
// backend reports "listening" (which proves Core spawned the SAPI plugin),
// then cancel. It does not need anyone to speak.
int run_voice_probe() {
    const std::string owner = "ipc-shell-voice-probe";
    std::uint64_t request_id = 1;
    auto decoded = voice_exchange(owo::protocol::MessageType::voice_start_request,
                                  request_id, owner + "\nzh-CN", 8000,
                                  std::chrono::milliseconds(2000));
    std::cout << "start -> " << voice_state(decoded) << '\n';
    if (!decoded.validation ||
        decoded.message.type == owo::protocol::MessageType::error_response) {
        return 3;
    }
    bool saw_listening = false;
    for (int attempt = 0; attempt < 15; ++attempt) {
        std::this_thread::sleep_for(std::chrono::milliseconds(150));
        decoded = voice_exchange(owo::protocol::MessageType::voice_poll_request,
                                 ++request_id, owner, 0,
                                 std::chrono::milliseconds(1500));
        const auto state = voice_state(decoded);
        std::cout << "poll -> " << state << '\n';
        if (state.rfind("listening", 0) == 0) {
            saw_listening = true;
            break;
        }
        if (decoded.validation &&
            decoded.message.type == owo::protocol::MessageType::error_response) {
            break;
        }
    }
    decoded = voice_exchange(owo::protocol::MessageType::voice_cancel_request,
                             ++request_id, owner, 0, std::chrono::milliseconds(1500));
    std::cout << "cancel -> " << voice_state(decoded) << '\n';
    if (saw_listening) {
        std::cout << "VOICE_BACKEND_OK: Core spawned the SAPI plugin and it is listening.\n";
        return 0;
    }
    std::cout << "VOICE_BACKEND_NOT_LISTENING: see states above.\n";
    return 4;
}

}  // namespace

int main(int argc, char** argv) {
    const std::string_view first = argc > 1 ? std::string_view(argv[1]) : std::string_view();
    const bool shutdown = first == "--shutdown";
    const bool update = first == "--update";
    const bool voice = first == "--voice";
    if (voice) return run_voice_probe();
    const std::string input = argc > 1 && !shutdown && !update ? argv[1] : "test";
    const owo::protocol::Message request{
        shutdown ? owo::protocol::MessageType::shutdown_request
                 : update ? owo::protocol::MessageType::candidate_update_request
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
        : update ? owo::protocol::MessageType::candidate_update_response
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
        if (!decoded.message.syllables.empty()) {
            for (std::size_t index = 0; index < decoded.message.syllables.size(); ++index) {
                if (index != 0) std::cout << '\'';
                std::cout << decoded.message.syllables[index];
            }
            std::cout << '\n';
        }
        for (std::size_t index = 0; index < decoded.message.candidates.size(); ++index) {
            std::cout << index + 1 << ". " << decoded.message.candidates[index] << '\n';
        }
        if (decoded.message.model_pending) std::cout << "model_pending\n";
    }
    return 0;
}
