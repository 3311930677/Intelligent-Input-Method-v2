#include "owo/engine/binary_lexicon.h"

#include <algorithm>
#include <array>
#include <fstream>
#include <iterator>
#include <limits>

namespace owo::engine {
namespace {

constexpr std::array<unsigned char, 4> kMagic{'O', 'W', 'L', 'X'};
constexpr std::size_t kHeaderSize = 20;
constexpr std::size_t kMaximumFileBytes = 256U * 1024U * 1024U;
constexpr std::size_t kMaximumEntries = 4U * 1024U * 1024U;

void append_u16(std::vector<unsigned char>& out, const std::uint16_t value) {
    out.push_back(static_cast<unsigned char>(value));
    out.push_back(static_cast<unsigned char>(value >> 8U));
}

void append_u32(std::vector<unsigned char>& out, const std::uint32_t value) {
    for (unsigned shift = 0; shift < 32; shift += 8) out.push_back(static_cast<unsigned char>(value >> shift));
}

void append_u64(std::vector<unsigned char>& out, const std::uint64_t value) {
    for (unsigned shift = 0; shift < 64; shift += 8) out.push_back(static_cast<unsigned char>(value >> shift));
}

std::uint64_t checksum(const std::span<const unsigned char> bytes) {
    std::uint64_t value = 14695981039346656037ULL;
    for (const auto byte : bytes) {
        value ^= byte;
        value *= 1099511628211ULL;
    }
    return value;
}

template <typename Integer>
bool read_integer(const std::span<const unsigned char> bytes, std::size_t& offset, Integer& value) {
    if (offset + sizeof(Integer) > bytes.size()) return false;
    value = 0;
    for (std::size_t index = 0; index < sizeof(Integer); ++index)
        value |= static_cast<Integer>(bytes[offset + index]) << (index * 8U);
    offset += sizeof(Integer);
    return true;
}

bool entry_less(const LexiconEntry& left, const LexiconEntry& right) {
    if (left.syllables != right.syllables) return left.syllables < right.syllables;
    if (left.text != right.text) return left.text < right.text;
    return left.frequency > right.frequency;
}

int compare_syllables(const std::vector<std::string>& left,
                      const std::span<const std::string_view> right) {
    const auto shared = std::min(left.size(), right.size());
    for (std::size_t index = 0; index < shared; ++index) {
        if (left[index] < right[index]) return -1;
        if (left[index] > right[index]) return 1;
    }
    if (left.size() < right.size()) return -1;
    if (left.size() > right.size()) return 1;
    return 0;
}

}  // namespace

LexiconIoResult write_binary_lexicon(const std::filesystem::path& path,
                                     std::vector<LexiconEntry> entries) {
    if (entries.size() > kMaximumEntries) return {false, "too many entries"};
    std::sort(entries.begin(), entries.end(), entry_less);
    std::vector<unsigned char> payload;
    for (const auto& entry : entries) {
        if (entry.text.empty() || entry.text.size() > std::numeric_limits<std::uint16_t>::max() ||
            entry.syllables.empty() || entry.syllables.size() > std::numeric_limits<std::uint16_t>::max())
            return {false, "entry field is empty or too large"};
        append_u32(payload, entry.frequency);
        append_u16(payload, static_cast<std::uint16_t>(entry.syllables.size()));
        append_u16(payload, static_cast<std::uint16_t>(entry.text.size()));
        for (const auto& syllable : entry.syllables) {
            if (syllable.empty() || syllable.size() > 255) return {false, "invalid syllable length"};
            payload.push_back(static_cast<unsigned char>(syllable.size()));
            payload.insert(payload.end(), syllable.begin(), syllable.end());
        }
        payload.insert(payload.end(), entry.text.begin(), entry.text.end());
        if (payload.size() + kHeaderSize > kMaximumFileBytes) return {false, "lexicon exceeds size limit"};
    }

    std::vector<unsigned char> output(kMagic.begin(), kMagic.end());
    append_u32(output, kBinaryLexiconVersion);
    append_u32(output, static_cast<std::uint32_t>(entries.size()));
    append_u64(output, checksum(payload));
    output.insert(output.end(), payload.begin(), payload.end());
    std::ofstream stream(path, std::ios::binary | std::ios::trunc);
    if (!stream) return {false, "cannot open output"};
    stream.write(reinterpret_cast<const char*>(output.data()), static_cast<std::streamsize>(output.size()));
    if (!stream) return {false, "cannot write output"};
    return {true, {}};
}

LexiconIoResult BinaryLexicon::load(const std::filesystem::path& path) {
    std::ifstream stream(path, std::ios::binary);
    if (!stream) return {false, "cannot open lexicon"};
    std::vector<unsigned char> bytes((std::istreambuf_iterator<char>(stream)), {});
    if (bytes.size() < kHeaderSize || bytes.size() > kMaximumFileBytes)
        return {false, "invalid lexicon size"};
    if (!std::equal(kMagic.begin(), kMagic.end(), bytes.begin())) return {false, "invalid magic"};
    std::size_t offset = kMagic.size();
    std::uint32_t version{};
    std::uint32_t count{};
    std::uint64_t expected_checksum{};
    if (!read_integer(std::span(bytes), offset, version) || version != kBinaryLexiconVersion)
        return {false, "unsupported lexicon version"};
    if (!read_integer(std::span(bytes), offset, count) || count > kMaximumEntries ||
        !read_integer(std::span(bytes), offset, expected_checksum)) return {false, "invalid header"};
    if (checksum(std::span(bytes).subspan(offset)) != expected_checksum) return {false, "checksum mismatch"};

    std::vector<LexiconEntry> parsed;
    parsed.reserve(count);
    for (std::uint32_t index = 0; index < count; ++index) {
        LexiconEntry entry;
        std::uint16_t syllable_count{};
        std::uint16_t text_size{};
        if (!read_integer(std::span(bytes), offset, entry.frequency) ||
            !read_integer(std::span(bytes), offset, syllable_count) || syllable_count == 0 ||
            !read_integer(std::span(bytes), offset, text_size) || text_size == 0)
            return {false, "truncated entry header"};
        entry.syllables.reserve(syllable_count);
        for (std::uint16_t syllable_index = 0; syllable_index < syllable_count; ++syllable_index) {
            if (offset >= bytes.size()) return {false, "truncated syllable"};
            const auto size = bytes[offset++];
            if (size == 0 || offset + size > bytes.size()) return {false, "invalid syllable"};
            entry.syllables.emplace_back(reinterpret_cast<const char*>(bytes.data() + offset), size);
            offset += size;
        }
        if (offset + text_size > bytes.size()) return {false, "truncated text"};
        entry.text.assign(reinterpret_cast<const char*>(bytes.data() + offset), text_size);
        offset += text_size;
        parsed.push_back(std::move(entry));
    }
    if (offset != bytes.size()) return {false, "trailing data"};
    if (!std::is_sorted(parsed.begin(), parsed.end(), entry_less))
        return {false, "entries are not sorted"};
    entries_ = std::move(parsed);
    return {true, {}};
}

std::vector<LexiconEntry> BinaryLexicon::lookup(const std::span<const std::string_view> syllables) const {
    const auto first = std::lower_bound(
        entries_.begin(), entries_.end(), syllables,
        [](const LexiconEntry& entry, const std::span<const std::string_view> reading) {
            return compare_syllables(entry.syllables, reading) < 0;
        });
    const auto last = std::upper_bound(
        first, entries_.end(), syllables,
        [](const std::span<const std::string_view> reading, const LexiconEntry& entry) {
            return compare_syllables(entry.syllables, reading) > 0;
        });
    return {first, last};
}

std::vector<LexiconEntry> BinaryLexicon::lookup_initial(const char initial) const {
    if (initial < 'a' || initial > 'z') return {};
    const std::string lower(1, initial);
    const std::string upper(1, static_cast<char>(initial + 1));
    const auto first = std::lower_bound(
        entries_.begin(), entries_.end(), lower,
        [](const LexiconEntry& entry, const std::string_view boundary) {
            return entry.syllables.empty() || entry.syllables.front() < boundary;
        });
    const auto last = std::lower_bound(
        first, entries_.end(), upper,
        [](const LexiconEntry& entry, const std::string_view boundary) {
            return entry.syllables.empty() || entry.syllables.front() < boundary;
        });
    std::vector<LexiconEntry> matches;
    for (auto entry = first; entry != last; ++entry) {
        if (entry->syllables.size() == 1 && !entry->syllables.front().empty() &&
            entry->syllables.front().front() == initial)
            matches.push_back(*entry);
    }
    return matches;
}

}  // namespace owo::engine
