#pragma once

#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace owo::engine {

struct LexiconEntry {
    std::vector<std::string> syllables;
    std::string text;
    std::uint32_t frequency{};
};

struct AbbreviatedLexiconMatch {
    LexiconEntry entry;
    std::vector<std::string> source_segments;
};

class Lexicon {
public:
    virtual ~Lexicon() = default;
    [[nodiscard]] virtual std::vector<LexiconEntry> lookup(
        std::span<const std::string_view> syllables) const = 0;
    [[nodiscard]] virtual std::vector<LexiconEntry> lookup_initial(char initial) const = 0;
    [[nodiscard]] virtual std::vector<AbbreviatedLexiconMatch> lookup_mixed_abbreviation(
        std::string_view input, std::size_t limit) const = 0;
    // Pure initial-letter abbreviation: every character of `input` is the first
    // letter of one syllable (e.g. "xz" -> xian/zai -> 现在). Unlike
    // lookup_mixed_abbreviation the first syllable is also abbreviated, so this
    // catches high-frequency whole-words that the per-character bigram fallback
    // misses (e.g. 现在/选择 for "xz").
    [[nodiscard]] virtual std::vector<AbbreviatedLexiconMatch> lookup_pure_abbreviation(
        std::string_view input, std::size_t limit) const = 0;
    [[nodiscard]] virtual std::size_t maximum_reading_length() const noexcept = 0;
};

class MemoryLexicon final : public Lexicon {
public:
    explicit MemoryLexicon(std::vector<LexiconEntry> entries);
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

private:
    std::vector<LexiconEntry> entries_;
    std::size_t maximum_reading_length_{};
};

}  // namespace owo::engine
