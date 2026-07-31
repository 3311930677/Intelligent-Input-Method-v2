#include "owo/engine/user_frequency.h"

#ifdef _WIN32
#include <Windows.h>
#endif

#include <algorithm>
#include <array>
#include <fstream>
#include <iterator>
#include <limits>
#include <span>
#include <vector>

namespace owo::engine {
namespace {

constexpr std::array<unsigned char, 4> kMagic{'O', 'W', 'U', 'F'};
constexpr std::uint32_t kVersion = 1;
constexpr std::size_t kHeaderSize = 20;
constexpr std::size_t kMaximumFileBytes = 16U * 1024U * 1024U;

void append_u32(std::vector<unsigned char>& out, const std::uint32_t value) {
    for (unsigned shift = 0; shift < 32; shift += 8) out.push_back(static_cast<unsigned char>(value >> shift));
}
void append_u64(std::vector<unsigned char>& out, const std::uint64_t value) {
    for (unsigned shift = 0; shift < 64; shift += 8) out.push_back(static_cast<unsigned char>(value >> shift));
}
std::uint64_t checksum(const std::span<const unsigned char> bytes) {
    std::uint64_t value = 14695981039346656037ULL;
    for (const auto byte : bytes) { value ^= byte; value *= 1099511628211ULL; }
    return value;
}
bool read_u32(const std::span<const unsigned char> bytes, std::size_t& offset, std::uint32_t& value) {
    if (offset + 4 > bytes.size()) return false;
    value = 0;
    for (unsigned index = 0; index < 4; ++index) value |= static_cast<std::uint32_t>(bytes[offset + index]) << (index * 8U);
    offset += 4;
    return true;
}
bool read_u64(const std::span<const unsigned char> bytes, std::size_t& offset, std::uint64_t& value) {
    if (offset + 8 > bytes.size()) return false;
    value = 0;
    for (unsigned index = 0; index < 8; ++index) value |= static_cast<std::uint64_t>(bytes[offset + index]) << (index * 8U);
    offset += 8;
    return true;
}

bool decode(const std::filesystem::path& path,
            std::unordered_map<std::string, std::uint32_t>& counts) {
    std::ifstream input(path, std::ios::binary);
    if (!input) return false;
    std::vector<unsigned char> bytes((std::istreambuf_iterator<char>(input)), {});
    if (bytes.size() < kHeaderSize || bytes.size() > kMaximumFileBytes ||
        !std::equal(kMagic.begin(), kMagic.end(), bytes.begin())) return false;
    std::size_t offset = 4;
    std::uint32_t version{}, entry_count{};
    std::uint64_t expected{};
    if (!read_u32(bytes, offset, version) || version != kVersion ||
        !read_u32(bytes, offset, entry_count) || !read_u64(bytes, offset, expected) ||
        checksum(std::span(bytes).subspan(offset)) != expected) return false;
    std::unordered_map<std::string, std::uint32_t> parsed;
    for (std::uint32_t index = 0; index < entry_count; ++index) {
        std::uint32_t size{}, count{};
        if (!read_u32(bytes, offset, size) || size == 0 || offset + size > bytes.size()) return false;
        std::string text(reinterpret_cast<const char*>(bytes.data() + offset), size);
        offset += size;
        if (!read_u32(bytes, offset, count) || count == 0 || !parsed.emplace(std::move(text), count).second) return false;
    }
    if (offset != bytes.size()) return false;
    counts = std::move(parsed);
    return true;
}

std::vector<unsigned char> encode(const std::unordered_map<std::string, std::uint32_t>& counts) {
    std::vector<std::pair<std::string, std::uint32_t>> ordered(counts.begin(), counts.end());
    std::sort(ordered.begin(), ordered.end());
    std::vector<unsigned char> payload;
    for (const auto& [text, count] : ordered) {
        append_u32(payload, static_cast<std::uint32_t>(text.size()));
        payload.insert(payload.end(), text.begin(), text.end());
        append_u32(payload, count);
    }
    std::vector<unsigned char> output(kMagic.begin(), kMagic.end());
    append_u32(output, kVersion);
    append_u32(output, static_cast<std::uint32_t>(ordered.size()));
    append_u64(output, checksum(payload));
    output.insert(output.end(), payload.begin(), payload.end());
    return output;
}

}  // namespace

UserFrequencyIoResult UserFrequencyStore::load(const std::filesystem::path& path) {
    path_ = path;
    counts_.clear();
    if (!std::filesystem::exists(path)) return {true, false, {}};
    if (decode(path, counts_)) return {true, false, {}};
    auto backup = path; backup += L".bak";
    if (decode(backup, counts_)) return {true, true, {}};
    return {false, false, "user frequency and backup are invalid"};
}

void UserFrequencyStore::record(const std::string_view text, const std::uint32_t amount) {
    if (text.empty() || amount == 0) return;
    auto& value = counts_[std::string(text)];
    value = (std::numeric_limits<std::uint32_t>::max)() - value < amount
                ? (std::numeric_limits<std::uint32_t>::max)() : value + amount;
}

std::uint32_t UserFrequencyStore::count(const std::string_view text) const {
    const auto found = counts_.find(std::string(text));
    return found == counts_.end() ? 0 : found->second;
}

std::int64_t UserFrequencyStore::score(const std::string_view text) const {
    return static_cast<std::int64_t>(count(text)) * 750;
}

UserFrequencyIoResult UserFrequencyStore::flush() const {
    if (path_.empty()) return {false, false, "user frequency path is not set"};
    const auto bytes = encode(counts_);
    if (bytes.size() > kMaximumFileBytes) return {false, false, "user frequency exceeds size limit"};
    auto temporary = path_; temporary += L".tmp";
    auto backup = path_; backup += L".bak";
    { std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
      if (!output) return {false, false, "cannot open temporary file"};
      output.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
      output.flush();
      if (!output) return {false, false, "cannot write temporary file"}; }
#ifdef _WIN32
    if (std::filesystem::exists(path_) && !CopyFileW(path_.c_str(), backup.c_str(), FALSE))
        return {false, false, "cannot update backup"};
    if (!MoveFileExW(temporary.c_str(), path_.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
        return {false, false, "cannot atomically replace user frequency"};
#else
    std::error_code error;
    if (std::filesystem::exists(path_)) std::filesystem::copy_file(path_, backup, std::filesystem::copy_options::overwrite_existing, error);
    std::filesystem::rename(temporary, path_, error);
    if (error) return {false, false, "cannot replace user frequency"};
#endif
    return {true, false, {}};
}

}  // namespace owo::engine
