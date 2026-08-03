#include "owo/voice/voice_protocol.h"

#include <string>
#include <vector>

namespace {

bool same_grant(const owo::voice::CapabilityGrant& left,
                const owo::voice::CapabilityGrant& right) {
    return left.grant_id == right.grant_id && left.subject == right.subject &&
           left.scope == right.scope && left.expires_at_unix_ms == right.expires_at_unix_ms &&
           left.max_uses == right.max_uses && left.risk_level == right.risk_level &&
           left.capabilities == right.capabilities;
}

bool same_message(const owo::voice::VoiceMessage& left,
                  const owo::voice::VoiceMessage& right) {
    return left.type == right.type && left.status == right.status &&
           left.request_id == right.request_id &&
           left.target_request_id == right.target_request_id &&
           left.timeout_ms == right.timeout_ms && left.plugin_id == right.plugin_id &&
           left.language == right.language && left.text == right.text &&
           left.diagnostic == right.diagnostic && left.capabilities == right.capabilities &&
           same_grant(left.grant, right.grant);
}

bool rejected(const std::string& bytes) {
    return !owo::voice::decode_voice_message(bytes).validation;
}

}  // namespace

int main() {
    using namespace owo::voice;

    VoiceMessage hello;
    hello.type = VoiceMessageType::hello_request;
    hello.status = VoiceStatus::success;
    hello.request_id = 1;
    hello.plugin_id = std::string(kVoicePluginId);
    hello.capabilities = {std::string(kMicrophoneCapability),
                          std::string(kTranscribeCapability)};
    const auto hello_bytes = encode_voice_message(hello);
    const auto hello_decoded = decode_voice_message(hello_bytes);
    if (hello_bytes.empty() || !hello_decoded.validation ||
        !same_message(hello, hello_decoded.message)) return 1;

    VoiceMessage start;
    start.type = VoiceMessageType::start_request;
    start.status = VoiceStatus::success;
    start.request_id = 42;
    start.timeout_ms = 10'000;
    start.plugin_id = std::string(kVoicePluginId);
    start.language = "zh-CN";
    start.grant.grant_id = "grant-0123456789abcdef";
    start.grant.subject = std::string(kVoicePluginId);
    start.grant.scope = std::string(kActiveCompositionScope);
    start.grant.expires_at_unix_ms = 4'102'444'800'000ULL;
    start.grant.max_uses = 1;
    start.grant.risk_level = RiskLevel::r3;
    start.grant.capabilities = {std::string(kMicrophoneCapability)};
    const auto start_bytes = encode_voice_message(start);
    const auto start_decoded = decode_voice_message(start_bytes);
    if (start_bytes.empty() || !start_decoded.validation ||
        !same_message(start, start_decoded.message)) return 2;

    VoiceMessage partial;
    partial.type = VoiceMessageType::partial_result;
    partial.status = VoiceStatus::success;
    partial.request_id = start.request_id;
    partial.plugin_id = std::string(kVoicePluginId);
    partial.text = "你好";
    if (!decode_voice_message(encode_voice_message(partial)).validation) return 3;

    VoiceMessage cancel;
    cancel.type = VoiceMessageType::cancel_request;
    cancel.status = VoiceStatus::success;
    cancel.request_id = 43;
    cancel.target_request_id = start.request_id;
    cancel.plugin_id = std::string(kVoicePluginId);
    if (!decode_voice_message(encode_voice_message(cancel)).validation) return 4;
    cancel.target_request_id = cancel.request_id;
    if (!encode_voice_message(cancel).empty()) return 5;

    auto malformed = start_bytes;
    malformed[4] = 2;
    if (decode_voice_message(malformed).validation.error !=
        owo::protocol::ErrorCode::unsupported_protocol) return 6;
    malformed = start_bytes;
    malformed[10] = 1;
    if (!rejected(malformed)) return 7;
    malformed = start_bytes;
    malformed.append("trailing");
    if (!rejected(malformed)) return 8;

    start.grant.risk_level = static_cast<RiskLevel>(9);
    if (!encode_voice_message(start).empty()) return 9;
    start.grant.risk_level = RiskLevel::r3;
    start.grant.capabilities = {std::string(kMicrophoneCapability),
                                std::string(kMicrophoneCapability)};
    if (!encode_voice_message(start).empty()) return 10;
    start.grant.capabilities = {std::string(kMicrophoneCapability)};
    start.language = "zh--CN";
    if (!encode_voice_message(start).empty()) return 11;

    VoiceMessage error;
    error.type = VoiceMessageType::error_response;
    error.status = VoiceStatus::permission_denied;
    error.request_id = 44;
    error.plugin_id = std::string(kVoicePluginId);
    error.diagnostic = "microphone capability was not granted";
    if (!decode_voice_message(encode_voice_message(error)).validation) return 12;
    error.diagnostic = std::string("\xc0\x80", 2);
    if (!encode_voice_message(error).empty()) return 13;

    return 0;
}
