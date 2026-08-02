#pragma once

#include "owo/engine/lexicon.h"

#include <filesystem>
#include <string>
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
    [[nodiscard]] std::size_t size() const noexcept { return entries_.size(); }
    [[nodiscard]] std::span<const LexiconEntry> entries() const noexcept { return entries_; }

private:
    std::vector<LexiconEntry> entries_;
};

}  // namespace owo::engine
