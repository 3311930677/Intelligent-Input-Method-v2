#include "owo/protocol/messages.h"

#include <iostream>

int main() {
    using namespace owo::protocol;
    int failures = 0;
    Message original{MessageType::candidate_response, 9, 4, "你好",
                     {"你好", "你号", "a\\\"b\n中文"}, 2, true};
    original.syllables = {"ni", "hao"};
    original.candidate_consumed = {5, 5, 5};
    original.expanded = true;
    original.page_size = 5;
    original.correction_enabled = false;
    const auto encoded = encode_message(original);
    const auto decoded = decode_message(encoded);
    if (!decoded.validation || decoded.message.type != original.type ||
        decoded.message.request_id != original.request_id ||
        decoded.message.context_generation != original.context_generation ||
        decoded.message.text != original.text ||
        decoded.message.candidates != original.candidates ||
        decoded.message.page != original.page ||
        decoded.message.has_more != original.has_more ||
        decoded.message.model_pending != original.model_pending ||
        decoded.message.syllables != original.syllables ||
        decoded.message.candidate_consumed != original.candidate_consumed ||
        decoded.message.expanded != original.expanded ||
        decoded.message.page_size != original.page_size ||
        decoded.message.correction_enabled != original.correction_enabled) {
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
    auto forged_plugin_call = encoded;
    const auto response_type = std::string("\"type\":\"candidate_response\"");
    const auto type_offset = forged_plugin_call.find(response_type);
    if (type_offset == std::string::npos) {
        std::cerr << "encoded message type missing\n";
        ++failures;
    } else {
        forged_plugin_call.replace(type_offset, response_type.size(),
                                   "\"type\":\"plugin_invoke\"");
    }
    if (decode_message(forged_plugin_call).validation.error != ErrorCode::invalid_payload) {
        std::cerr << "forged plugin invocation type was accepted\n";
        ++failures;
    }
    auto invalid_syllable = original;
    invalid_syllable.syllables = {"ni", "Hao"};
    if (!encode_message(invalid_syllable).empty()) {
        std::cerr << "invalid syllable was encoded\n";
        ++failures;
    }
    auto invalid_consumption = original;
    invalid_consumption.candidate_consumed.pop_back();
    if (!encode_message(invalid_consumption).empty()) {
        std::cerr << "misaligned candidate consumption was encoded\n";
        ++failures;
    }
    auto invalid_page_size = original;
    invalid_page_size.page_size = 10;
    if (!encode_message(invalid_page_size).empty()) {
        std::cerr << "invalid candidate page size was encoded\n";
        ++failures;
    }

    constexpr std::string_view legacy_request =
        R"({"protocol_version":5,"type":"candidate_request","request_id":41,"context_generation":7,"text":"nihao","candidates":[],"page":0,"has_more":false,"model_pending":false,"syllables":[],"candidate_consumed":[]})";
    if (decode_message(legacy_request).validation.error != ErrorCode::unsupported_protocol) {
        std::cerr << "current client decoder accepted legacy protocol\n";
        ++failures;
    }
    const auto legacy_decoded = decode_core_request(legacy_request);
    if (!legacy_decoded.validation ||
        legacy_decoded.protocol_version != kLegacyCoreProtocolVersion ||
        legacy_decoded.message.request_id != 41 || legacy_decoded.message.text != "nihao" ||
        legacy_decoded.message.expanded || legacy_decoded.message.page_size != 0 ||
        !legacy_decoded.message.correction_enabled) {
        std::cerr << "core did not map legacy request defaults\n";
        ++failures;
    }

    Message legacy_response{MessageType::candidate_response, 41, 7, "你好",
                            {"你好", "你号"}, 0, false};
    legacy_response.syllables = {"ni", "hao"};
    legacy_response.candidate_consumed = {5, 5};
    legacy_response.page_size = 5;
    const auto legacy_encoded = encode_core_response(
        legacy_response, kLegacyCoreProtocolVersion);
    if (legacy_encoded.empty() ||
        legacy_encoded.find("\"protocol_version\":5") == std::string::npos ||
        legacy_encoded.find("\"expanded\"") != std::string::npos ||
        legacy_encoded.find("\"page_size\"") != std::string::npos ||
        legacy_encoded.find("\"correction_enabled\"") != std::string::npos ||
        !decode_core_request(legacy_encoded).validation) {
        std::cerr << "legacy response schema is not v5 compatible\n";
        ++failures;
    }

    const auto forged_legacy_tail = std::string(legacy_request.substr(
        0, legacy_request.size() - 1)) + ",\"expanded\":false}";
    if (decode_core_request(forged_legacy_tail).validation.error !=
        ErrorCode::invalid_payload) {
        std::cerr << "legacy request accepted v7 tail fields\n";
        ++failures;
    }
    auto version_six = std::string(legacy_request);
    version_six.replace(version_six.find("\"protocol_version\":5"),
                        std::string("\"protocol_version\":5").size(),
                        "\"protocol_version\":6");
    if (decode_core_request(version_six).validation.error !=
        ErrorCode::unsupported_protocol) {
        std::cerr << "core accepted unsupported protocol v6\n";
        ++failures;
    }

    auto oversized_legacy = legacy_response;
    oversized_legacy.syllables.assign(33, "a");
    if (!encode_core_response(oversized_legacy, kLegacyCoreProtocolVersion).empty()) {
        std::cerr << "legacy response exceeded v5 syllable limit\n";
        ++failures;
    }
    return failures == 0 ? 0 : 1;
}
