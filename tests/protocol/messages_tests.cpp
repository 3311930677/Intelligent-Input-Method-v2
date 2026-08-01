#include "owo/protocol/messages.h"

#include <iostream>

int main() {
    using namespace owo::protocol;
    int failures = 0;
    const Message original{MessageType::candidate_response, 9, 4, "你好",
                           {"你好", "你号", "a\\\"b\n中文"}, 2, true};
    const auto encoded = encode_message(original);
    const auto decoded = decode_message(encoded);
    if (!decoded.validation || decoded.message.type != original.type ||
        decoded.message.request_id != original.request_id ||
        decoded.message.context_generation != original.context_generation ||
        decoded.message.text != original.text ||
        decoded.message.candidates != original.candidates ||
        decoded.message.page != original.page ||
        decoded.message.has_more != original.has_more ||
        decoded.message.model_pending != original.model_pending) {
        std::cerr << "message round trip failed\n";
        ++failures;
    }
    if (decode_message("{}").validation.error != ErrorCode::invalid_payload) {
        std::cerr << "invalid schema was accepted\n";
        ++failures;
    }
    auto wrong_version = encoded;
    const auto version_text = "\"protocol_version\":" + std::to_string(kProtocolVersion);
    const auto version_offset = wrong_version.find(version_text);
    if (version_offset == std::string::npos) {
        std::cerr << "encoded protocol version missing\n";
        ++failures;
    } else {
        wrong_version.replace(version_offset, version_text.size(),
                              "\"protocol_version\":999");
    }
    if (decode_message(wrong_version).validation.error != ErrorCode::unsupported_protocol) {
        std::cerr << "wrong version was accepted\n";
        ++failures;
    }
    return failures == 0 ? 0 : 1;
}
