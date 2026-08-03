#pragma once

#include "owo/protocol/envelope.h"

#include <cstdint>
#include <string>
#include <vector>

namespace owo::protocol {

inline constexpr std::uint32_t kLegacyCoreProtocolVersion = 5;

enum class MessageType {
    candidate_request,
    candidate_response,
    candidate_update_request,
    candidate_update_response,
    candidate_committed,
    voice_start_request,
    voice_poll_request,
    voice_cancel_request,
    voice_response,
    acknowledgement,
    shutdown_request,
    error_response,
};

struct Message {
    MessageType type{MessageType::error_response};
    std::uint64_t request_id{};
    std::uint64_t context_generation{};
    std::string text;
    std::vector<std::string> candidates;
    std::uint64_t page{};
    bool has_more{};
    bool model_pending{};
    std::vector<std::string> syllables;
    std::vector<std::uint64_t> candidate_consumed;
    bool expanded{};
    std::uint64_t page_size{};
    bool correction_enabled{true};
};

struct DecodeResult {
    Message message;
    ValidationResult validation;
    std::uint32_t protocol_version{kProtocolVersion};
};

/// 将内部消息编码为 UTF-8 JSON。该表示尚不是稳定公共协议。
[[nodiscard]] std::string encode_message(const Message& message);

/// 严格解析当前版本内部消息；拒绝未知字段、重复字段和非法转义。
[[nodiscard]] DecodeResult decode_message(std::string_view json);

/// Core 服务端专用：严格接受当前 v7 或旧 TSF v5 请求，不接受其他版本。
[[nodiscard]] DecodeResult decode_core_request(std::string_view json);

/// Core 服务端专用：按请求使用的线协议版本编码响应。
[[nodiscard]] std::string encode_core_response(const Message& message,
                                               std::uint32_t protocol_version);

}  // namespace owo::protocol
