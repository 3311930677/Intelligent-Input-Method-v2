#include "owo/protocol/messages.h"

#include <array>
#include <charconv>
#include <optional>

namespace owo::protocol {
namespace {

std::string type_name(const MessageType type) {
    switch (type) {
        case MessageType::candidate_request: return "candidate_request";
        case MessageType::candidate_response: return "candidate_response";
        case MessageType::shutdown_request: return "shutdown_request";
        case MessageType::error_response: return "error_response";
    }
    return "error_response";
}

std::optional<MessageType> parse_type(const std::string_view value) {
    if (value == "candidate_request") return MessageType::candidate_request;
    if (value == "candidate_response") return MessageType::candidate_response;
    if (value == "shutdown_request") return MessageType::shutdown_request;
    if (value == "error_response") return MessageType::error_response;
    return std::nullopt;
}

std::string escape_json(const std::string_view value) {
    std::string result;
    result.reserve(value.size());
    for (const unsigned char ch : value) {
        switch (ch) {
            case '"': result += "\\\""; break;
            case '\\': result += "\\\\"; break;
            case '\b': result += "\\b"; break;
            case '\f': result += "\\f"; break;
            case '\n': result += "\\n"; break;
            case '\r': result += "\\r"; break;
            case '\t': result += "\\t"; break;
            default:
                if (ch < 0x20U) return {};
                result.push_back(static_cast<char>(ch));
        }
    }
    return result;
}

bool consume(std::string_view input, std::size_t& offset, const std::string_view token) {
    if (input.substr(offset, token.size()) != token) return false;
    offset += token.size();
    return true;
}

std::optional<std::uint64_t> parse_uint(std::string_view input, std::size_t& offset) {
    const auto begin = input.data() + offset;
    const auto end = input.data() + input.size();
    std::uint64_t value{};
    const auto result = std::from_chars(begin, end, value);
    if (result.ec != std::errc{} || result.ptr == begin) return std::nullopt;
    offset = static_cast<std::size_t>(result.ptr - input.data());
    return value;
}

std::optional<std::string> parse_string(std::string_view input, std::size_t& offset) {
    if (!consume(input, offset, "\"")) return std::nullopt;
    std::string result;
    while (offset < input.size()) {
        const char ch = input[offset++];
        if (ch == '"') return result;
        if (static_cast<unsigned char>(ch) < 0x20U) return std::nullopt;
        if (ch != '\\') {
            result.push_back(ch);
            continue;
        }
        if (offset >= input.size()) return std::nullopt;
        switch (input[offset++]) {
            case '"': result.push_back('"'); break;
            case '\\': result.push_back('\\'); break;
            case 'b': result.push_back('\b'); break;
            case 'f': result.push_back('\f'); break;
            case 'n': result.push_back('\n'); break;
            case 'r': result.push_back('\r'); break;
            case 't': result.push_back('\t'); break;
            default: return std::nullopt;
        }
    }
    return std::nullopt;
}

std::optional<std::vector<std::string>> parse_string_array(std::string_view input,
                                                           std::size_t& offset) {
    if (!consume(input, offset, "[")) return std::nullopt;
    std::vector<std::string> values;
    if (consume(input, offset, "]")) return values;
    while (true) {
        auto value = parse_string(input, offset);
        if (!value) return std::nullopt;
        values.push_back(std::move(*value));
        if (consume(input, offset, "]")) return values;
        if (!consume(input, offset, ",")) return std::nullopt;
    }
}

std::optional<bool> parse_bool(std::string_view input, std::size_t& offset) {
    if (consume(input, offset, "true")) return true;
    if (consume(input, offset, "false")) return false;
    return std::nullopt;
}

}  // namespace

std::string encode_message(const Message& message) {
    const auto escaped = escape_json(message.text);
    if (!message.text.empty() && escaped.empty()) return {};
    std::string encoded_candidates = "[";
    for (std::size_t index = 0; index < message.candidates.size(); ++index) {
        const auto candidate = escape_json(message.candidates[index]);
        if (!message.candidates[index].empty() && candidate.empty()) return {};
        if (index != 0) encoded_candidates += ',';
        encoded_candidates += "\"" + candidate + "\"";
    }
    encoded_candidates += ']';
    return "{\"protocol_version\":" + std::to_string(kProtocolVersion) +
           ",\"type\":\"" + type_name(message.type) +
           "\",\"request_id\":" + std::to_string(message.request_id) +
           ",\"context_generation\":" + std::to_string(message.context_generation) +
           ",\"text\":\"" + escaped + "\",\"candidates\":" + encoded_candidates +
           ",\"page\":" + std::to_string(message.page) +
           ",\"has_more\":" + (message.has_more ? "true" : "false") + "}";
}

DecodeResult decode_message(const std::string_view json) {
    DecodeResult output{};
    if (json.size() > kMaximumPayloadBytes) {
        output.validation = {ErrorCode::payload_too_large, "message exceeds limit"};
        return output;
    }

    std::size_t offset = 0;
    if (!consume(json, offset, "{\"protocol_version\":")) goto invalid;
    {
        const auto version = parse_uint(json, offset);
        if (!version || *version != kProtocolVersion) {
            output.validation = {ErrorCode::unsupported_protocol, "unsupported protocol version"};
            return output;
        }
    }
    if (!consume(json, offset, ",\"type\":")) goto invalid;
    {
        const auto type_text = parse_string(json, offset);
        if (!type_text) goto invalid;
        const auto type = parse_type(*type_text);
        if (!type) goto invalid;
        output.message.type = *type;
    }
    if (!consume(json, offset, ",\"request_id\":")) goto invalid;
    {
        const auto value = parse_uint(json, offset);
        if (!value) goto invalid;
        output.message.request_id = *value;
    }
    if (!consume(json, offset, ",\"context_generation\":")) goto invalid;
    {
        const auto value = parse_uint(json, offset);
        if (!value) goto invalid;
        output.message.context_generation = *value;
    }
    if (!consume(json, offset, ",\"text\":")) goto invalid;
    {
        const auto value = parse_string(json, offset);
        if (!value) goto invalid;
        output.message.text = *value;
    }
    if (!consume(json, offset, ",\"candidates\":")) goto invalid;
    {
        auto values = parse_string_array(json, offset);
        if (!values) goto invalid;
        output.message.candidates = std::move(*values);
    }
    if (!consume(json, offset, ",\"page\":")) goto invalid;
    {
        const auto value = parse_uint(json, offset);
        if (!value) goto invalid;
        output.message.page = *value;
    }
    if (!consume(json, offset, ",\"has_more\":")) goto invalid;
    {
        const auto value = parse_bool(json, offset);
        if (!value) goto invalid;
        output.message.has_more = *value;
    }
    if (!consume(json, offset, "}") || offset != json.size()) goto invalid;
    output.validation = {};
    return output;

invalid:
    output.validation = {ErrorCode::invalid_payload, "invalid internal message schema"};
    return output;
}

}  // namespace owo::protocol
