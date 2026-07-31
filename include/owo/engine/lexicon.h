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

class Lexicon {
public:
    virtual ~Lexicon() = default;
    [[nodiscard]] virtual std::vector<LexiconEntry> lookup(
        std::span<const std::string_view> syllables) const = 0;
};

class MemoryLexicon final : public Lexicon {
public:
    explicit MemoryLexicon(std::vector<LexiconEntry> entries);
    [[nodiscard]] std::vector<LexiconEntry> lookup(
        std::span<const std::string_view> syllables) const override;

private:
    std::vector<LexiconEntry> entries_;
};

}  // namespace owo::engine
