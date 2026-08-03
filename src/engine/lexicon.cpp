#include "owo/engine/lexicon.h"

#include <algorithm>
#include <utility>

namespace owo::engine {

MemoryLexicon::MemoryLexicon(std::vector<LexiconEntry> entries) : entries_(std::move(entries)) {
    for (const auto& entry : entries_)
        maximum_reading_length_ = std::max(maximum_reading_length_, entry.syllables.size());
}

std::vector<LexiconEntry> MemoryLexicon::lookup(
    const std::span<const std::string_view> syllables) const {
    std::vector<LexiconEntry> matches;
    for (const auto& entry : entries_) {
        if (entry.syllables.size() != syllables.size()) continue;
        if (std::equal(entry.syllables.begin(), entry.syllables.end(), syllables.begin(),
                       [](const std::string& left, const std::string_view right) {
                           return left == right;
                       })) {
            matches.push_back(entry);
        }
    }
    return matches;
}

std::vector<LexiconEntry> MemoryLexicon::lookup_initial(const char initial) const {
    std::vector<LexiconEntry> matches;
    for (const auto& entry : entries_) {
        if (entry.syllables.size() == 1 && !entry.syllables.front().empty() &&
            entry.syllables.front().front() == initial)
            matches.push_back(entry);
    }
    return matches;
}

}  // namespace owo::engine
