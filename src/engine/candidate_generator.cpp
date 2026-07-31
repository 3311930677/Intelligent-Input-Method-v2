#include "owo/engine/candidate_generator.h"

#include <algorithm>
#include <string_view>
#include <unordered_map>

namespace owo::engine {

std::vector<Candidate> CandidateGenerator::generate(const ParseResult& parsed,
                                                    const std::size_t limit) const {
    if (!parsed.valid || limit == 0) return {};

    std::unordered_map<std::string, Candidate> unique;
    for (const auto& path : parsed.paths) {
        if (std::any_of(path.syllables.begin(), path.syllables.end(),
                        [](const Syllable& value) { return !value.complete; })) continue;

        std::vector<std::string_view> reading;
        reading.reserve(path.syllables.size());
        for (const auto& syllable : path.syllables) reading.push_back(syllable.text);

        for (auto& entry : lexicon_.lookup(reading)) {
            Candidate candidate{entry.text, std::move(entry.syllables),
                                static_cast<std::int64_t>(entry.frequency)};
            const auto found = unique.find(candidate.text);
            if (found == unique.end() || candidate.score > found->second.score) {
                unique.insert_or_assign(candidate.text, std::move(candidate));
            }
        }
    }

    std::vector<Candidate> candidates;
    candidates.reserve(unique.size());
    for (auto& [text, candidate] : unique) candidates.push_back(std::move(candidate));
    std::sort(candidates.begin(), candidates.end(), [](const Candidate& left, const Candidate& right) {
        if (left.score != right.score) return left.score > right.score;
        if (left.syllables.size() != right.syllables.size())
            return left.syllables.size() < right.syllables.size();
        return left.text < right.text;
    });
    if (candidates.size() > limit) candidates.resize(limit);
    return candidates;
}

}  // namespace owo::engine
