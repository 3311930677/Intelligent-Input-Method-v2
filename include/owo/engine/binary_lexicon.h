#pragma once

#include "owo/engine/lexicon.h"

#include <filesystem>
#include <string>
#include <unordered_map>
#include <vector>

namespace owo::engine {

inline constexpr std::uint32_t kBinaryLexiconVersion = 1;

struct LexiconIoResult {
    bool success{};
    std::string error;
};

[[nodiscard]] LexiconIoResult write_binary_lexicon(
    const std::filesystem::path& path, std::vector<LexiconEntry> entries);

class BinaryLexicon final : public Lexicon {
public:
    [[nodiscard]] LexiconIoResult load(const std::filesystem::path& path);
    [[nodiscard]] std::vector<LexiconEntry> lookup(
        std::span<const std::string_view> syllables) const override;
    [[nodiscard]] std::vector<LexiconEntry> lookup_initial(char initial) const override;
    [[nodiscard]] std::vector<AbbreviatedLexiconMatch> lookup_mixed_abbreviation(
        std::string_view input, std::size_t limit) const override;
    [[nodiscard]] std::vector<AbbreviatedLexiconMatch> lookup_pure_abbreviation(
        std::string_view input, std::size_t limit) const override;
    [[nodiscard]] std::size_t maximum_reading_length() const noexcept override {
        return maximum_reading_length_;
    }
    [[nodiscard]] std::size_t size() const noexcept { return entries_.size(); }
    [[nodiscard]] std::span<const LexiconEntry> entries() const noexcept { return entries_; }

    // Touches every entry once after load so the OS keeps the dictionary pages
    // resident in the working set instead of trimming them while Core is idle,
    // which caused cold-query page faults (hundreds of ms) on real typing.
    void touch_pages() const;

private:
    std::vector<LexiconEntry> entries_;
    std::size_t maximum_reading_length_{};
    // Pure initial-letter abbreviation index: key = first letter of each
    // syllable (现在 xian/zai -> "xz"). Built once at load so
    // lookup_pure_abbreviation is O(1) instead of scanning a 100k+ entry block.
    std::unordered_map<std::string, std::vector<std::size_t>> initial_index_;
};

}  // namespace owo::engine
