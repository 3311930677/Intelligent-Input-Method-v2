#pragma once

#include "owo/protocol/envelope.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace owo::voice {

inline constexpr std::uint32_t kVoiceProtocolVersion = 1;
inline constexpr std::size_t kMaximumTranscriptBytes = 64U * 1024U;
inline constexpr std::uint32_t kMaximumRecognitionTimeoutMs = 60'000;

inline constexpr std::string_view kVoicePluginId = "owo.voice.input";
inline constexpr std::string_view kMicrophoneCapability = "microphone.capture";
inline constexpr std::string_view kTextCommitCapability = "text.commit.v1";
inline constexpr std::string_view kTranscribeCapability = "voice.transcribe.v1";
inline constexpr std::string_view kActiveCompositionScope = "active-composition";

enum class VoiceMessageType : std::uint8_t {
    hello_request = 1,
    hello_response = 2,
    start_request = 3,
    partial_result = 4,
    final_result = 5,
    cancel_request = 6,
    shutdown_request = 7,
    acknowledgement = 8,
    error_response = 9,
    listening_started = 10,
};

enum class VoiceStatus : std::uint8_t {
    success = 0,
    invalid_request = 1,
    permission_denied = 2,
    unsupported_protocol = 3,
    timeout = 4,
    cancelled = 5,
    recognizer_error = 6,
};

enum class RiskLevel : std::uint8_t {
    r0 = 0,
    r1 = 1,
    r2 = 2,
    r3 = 3,
    r4 = 4,
};

struct CapabilityGrant {
    std::string grant_id;
    std::string subject;
    std::string scope;
    std::uint64_t expires_at_unix_ms{};
    std::uint32_t max_uses{};
    RiskLevel risk_level{RiskLevel::r0};
    std::vector<std::string> capabilities;
};

struct VoiceMessage {
    VoiceMessageType type{VoiceMessageType::error_response};
    VoiceStatus status{VoiceStatus::recognizer_error};
    std::uint64_t request_id{};
    std::uint64_t target_request_id{};
    std::uint32_t timeout_ms{};
    std::string plugin_id;
    std::string language;
    std::string text;
    std::string diagnostic;
    std::vector<std::string> capabilities;
    CapabilityGrant grant;
};

struct VoiceDecodeResult {
    VoiceMessage message;
    protocol::ValidationResult validation;
};

[[nodiscard]] std::string encode_voice_message(const VoiceMessage& message);
[[nodiscard]] VoiceDecodeResult decode_voice_message(std::string_view payload);

}  // namespace owo::voice
