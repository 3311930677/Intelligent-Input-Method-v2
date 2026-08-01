#include "owo/plugin/package_signature.h"

#include "owo/plugin/package_archive.h"

#include <algorithm>
#include <array>
#include <charconv>
#include <cctype>
#include <limits>
#include <map>
#include <variant>

namespace owo::plugin {
namespace {

constexpr std::size_t kMaximumSignatureJsonBytes = 32U * 1024U;
constexpr std::size_t kMaximumCmsBytes = 24U * 1024U;
using Value = std::variant<std::string, std::uint64_t>;

class Parser final {
public:
    explicit Parser(const std::string_view input) : input_(input) {}

    bool object(std::map<std::string, Value>& fields) {
        space();
        if (!take('{')) return fail("signature metadata must be a JSON object");
        space();
        if (take('}')) return finish();
        while (true) {
            std::string key;
            if (!string(key)) return false;
            space();
            if (!take(':')) return fail("expected ':' after field name");
            space();
            Value value;
            if (key == "schema_version") {
                std::uint64_t number{};
                if (!unsigned_integer(number)) return false;
                value = number;
            } else if (key == "inventory_sha256" || key == "format" || key == "signature_base64") {
                std::string text;
                if (!string(text)) return false;
                value = std::move(text);
            } else {
                return fail("unknown field: " + key);
            }
            if (!fields.emplace(key, std::move(value)).second) return fail("duplicate field: " + key);
            space();
            if (take('}')) return finish();
            if (!take(',')) return fail("expected ',' or '}'");
            space();
        }
    }

    const std::string& error() const { return error_; }

private:
    void space() {
        while (offset_ < input_.size() &&
               (input_[offset_] == ' ' || input_[offset_] == '\t' ||
                input_[offset_] == '\r' || input_[offset_] == '\n')) ++offset_;
    }

    bool take(const char expected) {
        if (offset_ >= input_.size() || input_[offset_] != expected) return false;
        ++offset_;
        return true;
    }

    bool finish() {
        space();
        return offset_ == input_.size() || fail("trailing data after signature metadata");
    }

    bool string(std::string& output) {
        if (!take('"')) return fail("expected JSON string");
        while (offset_ < input_.size()) {
            const auto byte = static_cast<unsigned char>(input_[offset_++]);
            if (byte == '"') return true;
            if (byte < 0x20U || byte >= 0x80U) return fail("signature strings must be printable ASCII");
            if (byte == '\\') {
                if (offset_ >= input_.size()) return fail("truncated JSON escape");
                const auto escaped = input_[offset_++];
                if (escaped != '"' && escaped != '\\' && escaped != '/')
                    return fail("unsupported JSON escape");
                output.push_back(escaped);
            } else {
                output.push_back(static_cast<char>(byte));
            }
        }
        return fail("unterminated JSON string");
    }

    bool unsigned_integer(std::uint64_t& output) {
        const auto* begin = input_.data() + offset_;
        const auto* end = input_.data() + input_.size();
        const auto parsed = std::from_chars(begin, end, output);
        if (parsed.ec != std::errc{} || parsed.ptr == begin) return fail("expected unsigned integer");
        offset_ += static_cast<std::size_t>(parsed.ptr - begin);
        return true;
    }

    bool fail(std::string message) {
        if (error_.empty()) error_ = std::move(message) + " at byte " + std::to_string(offset_);
        return false;
    }

    std::string_view input_;
    std::size_t offset_{};
    std::string error_;
};

bool sha256_text(const std::string_view value) {
    return value.size() == 64 &&
           std::all_of(value.begin(), value.end(), [](const unsigned char byte) {
               return std::isdigit(byte) != 0 || (byte >= 'a' && byte <= 'f');
           });
}

bool decode_base64(const std::string_view input, std::vector<unsigned char>& output) {
    if (input.empty() || input.size() % 4 != 0 || input.size() > ((kMaximumCmsBytes + 2U) / 3U) * 4U)
        return false;
    std::array<int, 256> table{};
    table.fill(-1);
    constexpr std::string_view alphabet =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    for (std::size_t index = 0; index < alphabet.size(); ++index)
        table[static_cast<unsigned char>(alphabet[index])] = static_cast<int>(index);
    output.clear();
    output.reserve(input.size() / 4U * 3U);
    for (std::size_t offset = 0; offset < input.size(); offset += 4) {
        const bool final = offset + 4 == input.size();
        const char third = input[offset + 2];
        const char fourth = input[offset + 3];
        if ((!final && (third == '=' || fourth == '=')) || (third == '=' && fourth != '=')) return false;
        const int a = table[static_cast<unsigned char>(input[offset])];
        const int b = table[static_cast<unsigned char>(input[offset + 1])];
        const int c = third == '=' ? 0 : table[static_cast<unsigned char>(third)];
        const int d = fourth == '=' ? 0 : table[static_cast<unsigned char>(fourth)];
        if (a < 0 || b < 0 || c < 0 || d < 0) return false;
        if ((third == '=' && (b & 0x0f) != 0) || (fourth == '=' && third != '=' && (c & 0x03) != 0))
            return false;  // Reject non-canonical pad bits.
        const auto bits = static_cast<std::uint32_t>((a << 18) | (b << 12) | (c << 6) | d);
        output.push_back(static_cast<unsigned char>(bits >> 16U));
        if (third != '=') output.push_back(static_cast<unsigned char>(bits >> 8U));
        if (fourth != '=') output.push_back(static_cast<unsigned char>(bits));
    }
    return !output.empty() && output.size() <= kMaximumCmsBytes;
}

template <typename T>
bool get(const std::map<std::string, Value>& fields, const char* key, T& output) {
    const auto found = fields.find(key);
    if (found == fields.end()) return false;
    const auto* value = std::get_if<T>(&found->second);
    if (value == nullptr) return false;
    output = *value;
    return true;
}

}  // namespace

PackageSignatureResult parse_package_signature(const std::string_view json) {
    if (json.empty() || json.size() > kMaximumSignatureJsonBytes)
        return {false, {}, "signature metadata size is outside [1, 32768]"};
    if (json.size() >= 3 && static_cast<unsigned char>(json[0]) == 0xefU &&
        static_cast<unsigned char>(json[1]) == 0xbbU && static_cast<unsigned char>(json[2]) == 0xbfU)
        return {false, {}, "UTF-8 BOM is not accepted"};
    std::map<std::string, Value> fields;
    Parser parser(json);
    if (!parser.object(fields)) return {false, {}, parser.error()};
    if (fields.size() != 4) return {false, {}, "signature metadata must contain exactly four v1 fields"};
    std::uint64_t schema_version{};
    std::string base64;
    PackageSignature result;
    if (!get(fields, "schema_version", schema_version) ||
        !get(fields, "inventory_sha256", result.inventory_sha256) ||
        !get(fields, "format", result.format) || !get(fields, "signature_base64", base64))
        return {false, {}, "signature metadata is missing a required field"};
    if (schema_version != kPackageSignatureSchemaVersion)
        return {false, {}, "unsupported signature schema_version"};
    result.schema_version = static_cast<std::uint32_t>(schema_version);
    if (!sha256_text(result.inventory_sha256)) return {false, {}, "invalid inventory_sha256"};
    if (result.format != "cms-detached-sha256") return {false, {}, "unsupported signature format"};
    if (!decode_base64(base64, result.cms_der)) return {false, {}, "signature_base64 is not canonical bounded Base64"};
    if (result.cms_der.size() < 2 || result.cms_der.front() != 0x30U)
        return {false, {}, "CMS signature must be a DER SEQUENCE"};
    return {true, std::move(result), {}};
}

std::string package_signature_content(const std::string_view inventory_sha256) {
    if (!sha256_text(inventory_sha256)) return {};
    return "OwOPackageInventoryV1:" + std::string(inventory_sha256) + "\n";
}

SignedPackageMetadataResult inspect_signed_package_metadata(
    const std::filesystem::path& package_path) {
    const auto package = inspect_package(package_path);
    if (!package.ok) return {false, {}, {}, package.diagnostic};
    if (package.embedded_signature_json.empty())
        return {false, {}, {}, "root signature.json is required"};
    const auto signature = parse_package_signature(package.embedded_signature_json);
    if (!signature.ok) return {false, {}, {}, signature.diagnostic};
    if (signature.value.inventory_sha256 != package.inventory_sha256)
        return {false, {}, {}, "signature inventory_sha256 does not match package inventory"};
    return {true, package.inventory_sha256, signature.value, {}};
}

}  // namespace owo::plugin
