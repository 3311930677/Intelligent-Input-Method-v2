#include "owo/protocol/messages.h"

#include <iostream>

int main() {
    using namespace owo::protocol;
    int failures = 0;
    const Message original{MessageType::candidate_request, 9, 4, "a\\\"b\n中文"};
    const auto encoded = encode_message(original);
    const auto decoded = decode_message(encoded);
    if (!decoded.validation || decoded.message.type != original.type ||
        decoded.message.request_id != original.request_id ||
        decoded.message.context_generation != original.context_generation ||
        decoded.message.text != original.text) {
        std::cerr << "message round trip failed\n";
        ++failures;
    }
    if (decode_message("{}").validation.error != ErrorCode::invalid_payload) {
        std::cerr << "invalid schema was accepted\n";
        ++failures;
    }
    auto wrong_version = encoded;
    wrong_version.replace(wrong_version.find(":1"), 2, ":2");
    if (decode_message(wrong_version).validation.error != ErrorCode::unsupported_protocol) {
        std::cerr << "wrong version was accepted\n";
        ++failures;
    }
    return failures == 0 ? 0 : 1;
}

