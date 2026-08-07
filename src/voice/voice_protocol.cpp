#include "owo/voice/voice_protocol.h"

#include <algorithm>
#include <array>
#include <limits>
#include <utility>

namespace owo::voice {
namespace {

constexpr std::array<char, 4> kMagic{'O', 'W', 'V', 'H'};
constexpr std::size_t kMaximumCapabilities = 16;
constexpr std::size_t kMaximumDiagnosticBytes = 4096;
constexpr std::size_t kMaximumIdentifierBytes = 128;
constexpr std::size_t kMaximumLanguageBytes = 35;
constexpr std::uint32_t kMaximumGrantUses = 16;

template <typename Integer>
void append_integer(std::string& output, const Integer value) {
    for (std::size_t shift = 0; shift < sizeof(Integer) * 8U; shift += 8U)
        output.push_back(static_cast<char>(value >> shift));
}

bool append_string(std::string& output, const std::string_view value) {
    if (value.size() > std::numeric_limits<std::uint32_t>::max()) return false;
    append_integer(output, static_cast<std::uint32_t>(value.size()));
    output.append(value);
    return true;
}

template <typename Integer>
bool read_integer(const std::string_view input, std::size_t& offset, Integer& value) {
    if (offset + sizeof(Integer) > input.size()) return false;
    value = 0;
    for (std::size_t index = 0; index < sizeof(Integer); ++index)
        value |= static_cast<Integer>(static_cast<unsigned char>(input[offset + index]))
                 << (index * 8U);
    offset += sizeof(Integer);
    return true;
}

bool read_string(const std::string_view input, std::size_t& offset, std::string& value,
                 const std::size_t maximum) {
    std::uint32_t size{};
    if (!read_integer(input, offset, size) || size > maximum || offset + size > input.size())
        return false;
    value.assign(input.substr(offset, size));
    offset += size;
    return true;
}

bool valid_utf8(const std::string_view text) {
    std::size_t offset = 0;
    while (offset < text.size()) {
        const auto first = static_cast<unsigned char>(text[offset]);
        std::size_t count = 0;
        std::uint32_t scalar = 0;
        if (first <= 0x7fU) { count = 1; scalar = first; }
        else if (first >= 0xc2U && first <= 0xdfU) { count = 2; scalar = first & 0x1fU; }
        else if (first >= 0xe0U && first <= 0xefU) { count = 3; scalar = first & 0x0fU; }
        else if (first >= 0xf0U && first <= 0xf4U) { count = 4; scalar = first & 0x07U; }
        else return false;
        if (offset + count > text.size()) return false;
        for (std::size_t index = 1; index < count; ++index) {
            const auto next = static_cast<unsigned char>(text[offset + index]);
            if ((next & 0xc0U) != 0x80U) return false;
            scalar = (scalar << 6U) | (next & 0x3fU);
        }
        if ((count == 3 && scalar < 0x800U) || (count == 4 && scalar < 0x10000U) ||
            (scalar >= 0xd800U && scalar <= 0xdfffU) || scalar > 0x10ffffU) return false;
        offset += count;
    }
    return true;
}

bool token(const std::string_view value) {
    if (value.empty() || value.size() > kMaximumIdentifierBytes || value.front() == '.' ||
        value.back() == '.') return false;
    bool previous_dot = false;
    for (const unsigned char byte : value) {
        const bool valid = (byte >= 'a' && byte <= 'z') || (byte >= '0' && byte <= '9') ||
                           byte == '-' || byte == '_' || byte == '.';
        if (!valid || (byte == '.' && previous_dot)) return false;
        previous_dot = byte == '.';
    }
    return true;
}

bool grant_id(const std::string_view value) {
    if (value.size() < 16 || value.size() > kMaximumIdentifierBytes) return false;
    return std::all_of(value.begin(), value.end(), [](const unsigned char byte) {
        return (byte >= 'a' && byte <= 'z') || (byte >= 'A' && byte <= 'Z') ||
               (byte >= '0' && byte <= '9') || byte == '-' || byte == '_';
    });
}

bool language_tag(const std::string_view value) {
    if (value.size() < 2 || value.size() > kMaximumLanguageBytes || value.front() == '-' ||
        value.back() == '-') return false;
    bool previous_dash = false;
    for (const unsigned char byte : value) {
        const bool valid = (byte >= 'a' && byte <= 'z') || (byte >= 'A' && byte <= 'Z') ||
                           (byte >= '0' && byte <= '9') || byte == '-';
        if (!valid || (byte == '-' && previous_dash)) return false;
        previous_dash = byte == '-';
    }
    return true;
}

bool ordered_capabilities(const std::vector<std::string>& capabilities) {
    return capabilities.size() <= kMaximumCapabilities &&
           std::all_of(capabilities.begin(), capabilities.end(), token) &&
           std::is_sorted(capabilities.begin(), capabilities.end()) &&
           std::adjacent_find(capabilities.begin(), capabilities.end()) == capabilities.end();
}

bool empty_grant(const CapabilityGrant& grant) {
    return grant.grant_id.empty() && grant.subject.empty() && grant.scope.empty() &&
           grant.expires_at_unix_ms == 0 && grant.max_uses == 0 &&
           grant.risk_level == RiskLevel::r0 && grant.capabilities.empty();
}

bool valid_grant(const CapabilityGrant& grant) {
    return grant_id(grant.grant_id) && token(grant.subject) && token(grant.scope) &&
           grant.expires_at_unix_ms != 0 && grant.max_uses > 0 &&
           grant.max_uses <= kMaximumGrantUses && grant.risk_level >= RiskLevel::r0 &&
           grant.risk_level <= RiskLevel::r4 && !grant.capabilities.empty() &&
           ordered_capabilities(grant.capabilities);
}

bool valid_type(const VoiceMessageType type) {
    return type >= VoiceMessageType::hello_request && type <= VoiceMessageType::listening_started;
}

bool valid_status(const VoiceStatus status) {
    return status >= VoiceStatus::success && status <= VoiceStatus::recognizer_error;
}

bool valid_message(const VoiceMessage& message) {
    if (!valid_type(message.type) || !valid_status(message.status) || message.request_id == 0 ||
        !token(message.plugin_id) || message.text.size() > kMaximumTranscriptBytes ||
        message.diagnostic.size() > kMaximumDiagnosticBytes || !valid_utf8(message.text) ||
        !valid_utf8(message.diagnostic) || !ordered_capabilities(message.capabilities)) return false;

    const bool no_result = message.text.empty() && message.diagnostic.empty();
    switch (message.type) {
    case VoiceMessageType::hello_request:
    case VoiceMessageType::hello_response:
        return message.status == VoiceStatus::success && message.target_request_id == 0 &&
               message.timeout_ms == 0 && message.language.empty() && no_result &&
               !message.capabilities.empty() && empty_grant(message.grant);
    case VoiceMessageType::start_request:
        return message.status == VoiceStatus::success && message.target_request_id == 0 &&
               message.timeout_ms >= 100 && message.timeout_ms <= kMaximumRecognitionTimeoutMs &&
               language_tag(message.language) && no_result && message.capabilities.empty() &&
               valid_grant(message.grant);
    case VoiceMessageType::partial_result:
    case VoiceMessageType::final_result:
        return message.status == VoiceStatus::success && message.target_request_id == 0 &&
               message.timeout_ms == 0 && message.language.empty() && !message.text.empty() &&
               message.diagnostic.empty() && message.capabilities.empty() &&
               empty_grant(message.grant);
    case VoiceMessageType::listening_started:
        return message.status == VoiceStatus::success && message.target_request_id == 0 &&
               message.timeout_ms == 0 && message.language.empty() &&
               message.text.empty() && message.diagnostic.empty() &&
               message.capabilities.empty() && empty_grant(message.grant);
    case VoiceMessageType::cancel_request:
        return message.status == VoiceStatus::success && message.target_request_id != 0 &&
               message.target_request_id != message.request_id && message.timeout_ms == 0 &&
               message.language.empty() && no_result && message.capabilities.empty() &&
               empty_grant(message.grant);
    case VoiceMessageType::shutdown_request:
    case VoiceMessageType::acknowledgement:
        return message.status == VoiceStatus::success && message.target_request_id == 0 &&
               message.timeout_ms == 0 && message.language.empty() && no_result &&
               message.capabilities.empty() && empty_grant(message.grant);
    case VoiceMessageType::error_response:
        return message.status != VoiceStatus::success && message.target_request_id == 0 &&
               message.timeout_ms == 0 && message.language.empty() && message.text.empty() &&
               !message.diagnostic.empty() && message.capabilities.empty() &&
               empty_grant(message.grant);
    }
    return false;
}

}  // namespace

std::string encode_voice_message(const VoiceMessage& message) {
    if (!valid_message(message)) return {};
    std::string output(kMagic.begin(), kMagic.end());
    append_integer(output, kVoiceProtocolVersion);
    append_integer(output, static_cast<std::uint8_t>(message.type));
    append_integer(output, static_cast<std::uint8_t>(message.status));
    append_integer(output, static_cast<std::uint16_t>(0));
    append_integer(output, message.request_id);
    append_integer(output, message.target_request_id);
    append_integer(output, message.timeout_ms);
    append_integer(output, static_cast<std::uint8_t>(message.grant.risk_level));
    append_integer(output, static_cast<std::uint8_t>(0));
    append_integer(output, static_cast<std::uint16_t>(message.capabilities.size()));
    append_integer(output, static_cast<std::uint16_t>(message.grant.capabilities.size()));
    append_integer(output, static_cast<std::uint16_t>(0));
    append_integer(output, message.grant.expires_at_unix_ms);
    append_integer(output, message.grant.max_uses);
    if (!append_string(output, message.plugin_id) || !append_string(output, message.language) ||
        !append_string(output, message.text) || !append_string(output, message.diagnostic) ||
        !append_string(output, message.grant.grant_id) ||
        !append_string(output, message.grant.subject) ||
        !append_string(output, message.grant.scope)) return {};
    for (const auto& capability : message.capabilities)
        if (!append_string(output, capability)) return {};
    for (const auto& capability : message.grant.capabilities)
        if (!append_string(output, capability)) return {};
    if (output.size() > protocol::kMaximumPayloadBytes) return {};
    return output;
}

VoiceDecodeResult decode_voice_message(const std::string_view payload) {
    VoiceDecodeResult result;
    if (payload.size() > protocol::kMaximumPayloadBytes) {
        result.validation = {protocol::ErrorCode::payload_too_large,
                             "voice message exceeds transport limit"};
        return result;
    }
    if (payload.size() < kMagic.size() ||
        !std::equal(kMagic.begin(), kMagic.end(), payload.begin())) goto invalid;
    {
        std::size_t offset = kMagic.size();
        std::uint32_t version{};
        std::uint8_t type{};
        std::uint8_t status{};
        std::uint16_t reserved{};
        std::uint8_t risk{};
        std::uint8_t reserved_byte{};
        std::uint16_t capability_count{};
        std::uint16_t grant_capability_count{};
        std::uint16_t reserved_tail{};
        if (!read_integer(payload, offset, version) || version != kVoiceProtocolVersion) {
            result.validation = {protocol::ErrorCode::unsupported_protocol,
                                 "unsupported voice protocol version"};
            return result;
        }
        if (!read_integer(payload, offset, type) || !read_integer(payload, offset, status) ||
            !read_integer(payload, offset, reserved) || reserved != 0 ||
            !read_integer(payload, offset, result.message.request_id) ||
            !read_integer(payload, offset, result.message.target_request_id) ||
            !read_integer(payload, offset, result.message.timeout_ms) ||
            !read_integer(payload, offset, risk) ||
            !read_integer(payload, offset, reserved_byte) || reserved_byte != 0 ||
            !read_integer(payload, offset, capability_count) ||
            capability_count > kMaximumCapabilities ||
            !read_integer(payload, offset, grant_capability_count) ||
            grant_capability_count > kMaximumCapabilities ||
            !read_integer(payload, offset, reserved_tail) || reserved_tail != 0 ||
            !read_integer(payload, offset, result.message.grant.expires_at_unix_ms) ||
            !read_integer(payload, offset, result.message.grant.max_uses)) goto invalid;
        result.message.type = static_cast<VoiceMessageType>(type);
        result.message.status = static_cast<VoiceStatus>(status);
        result.message.grant.risk_level = static_cast<RiskLevel>(risk);
        if (!read_string(payload, offset, result.message.plugin_id, kMaximumIdentifierBytes) ||
            !read_string(payload, offset, result.message.language, kMaximumLanguageBytes) ||
            !read_string(payload, offset, result.message.text, kMaximumTranscriptBytes) ||
            !read_string(payload, offset, result.message.diagnostic, kMaximumDiagnosticBytes) ||
            !read_string(payload, offset, result.message.grant.grant_id,
                         kMaximumIdentifierBytes) ||
            !read_string(payload, offset, result.message.grant.subject,
                         kMaximumIdentifierBytes) ||
            !read_string(payload, offset, result.message.grant.scope,
                         kMaximumIdentifierBytes)) goto invalid;
        result.message.capabilities.reserve(capability_count);
        for (std::uint16_t index = 0; index < capability_count; ++index) {
            std::string capability;
            if (!read_string(payload, offset, capability, kMaximumIdentifierBytes)) goto invalid;
            result.message.capabilities.push_back(std::move(capability));
        }
        result.message.grant.capabilities.reserve(grant_capability_count);
        for (std::uint16_t index = 0; index < grant_capability_count; ++index) {
            std::string capability;
            if (!read_string(payload, offset, capability, kMaximumIdentifierBytes)) goto invalid;
            result.message.grant.capabilities.push_back(std::move(capability));
        }
        if (offset != payload.size() || !valid_message(result.message)) goto invalid;
    }
    result.validation = {};
    return result;

invalid:
    result.validation = {protocol::ErrorCode::invalid_payload, "invalid voice message schema"};
    return result;
}

}  // namespace owo::voice
