#include "owo/engine/candidate_generator.h"

#include <algorithm>
#include <cmath>
#include <string_view>
#include <unordered_map>

namespace owo::engine {
namespace {

struct SearchState {
    std::string text;
    std::string previous;
    std::vector<std::string> syllables;
    std::int64_t score{};
};

std::int64_t unigram_score(const std::uint32_t frequency) {
    return static_cast<std::int64_t>(std::log1p(static_cast<double>(frequency)) * 1000.0);
}

// Raw corpus frequencies are attached to entries of different lengths. Without
// a token transition cost, summing several high-frequency characters always
// overwhelms a valid multi-syllable word and rewards pathological over-segmentation.
constexpr std::int64_t kAdditionalSegmentPenalty = 22'000;
constexpr std::int64_t kIncompleteBasePenalty = 1'500;
constexpr std::int64_t kIncompleteCharacterPenalty = 750;
constexpr std::int64_t kCorrectionPenalty = 6'000;
constexpr std::int64_t kAbbreviationBasePenalty = 8'000;

std::int64_t input_match_penalty(const ParsePath& path) {
    if (path.match_kind == InputMatchKind::incomplete_completion) {
        return kIncompleteBasePenalty +
               static_cast<std::int64_t>(path.completion_characters) *
                   kIncompleteCharacterPenalty;
    }
    if (path.match_kind == InputMatchKind::corrected)
        return static_cast<std::int64_t>(path.edit_count) * kCorrectionPenalty;
    if (path.match_kind == InputMatchKind::abbreviated_completion) {
        return kAbbreviationBasePenalty +
               static_cast<std::int64_t>(path.completion_characters) *
                   kIncompleteCharacterPenalty;
    }
    return 0;
}

void prune(std::vector<SearchState>& states, const std::size_t width) {
    std::sort(states.begin(), states.end(), [](const SearchState& left, const SearchState& right) {
        if (left.score != right.score) return left.score > right.score;
        return left.text < right.text;
    });
    if (states.size() > width) states.resize(width);
}

}  // namespace

std::vector<Candidate> CandidateGenerator::generate(const ParseResult& parsed,
                                                    const std::size_t limit) const {
    if (!parsed.valid || limit == 0) return {};

    std::unordered_map<std::string, Candidate> unique;
    std::vector<const ParsePath*> ordered_paths;
    ordered_paths.reserve(parsed.paths.size());
    for (const auto kind : {InputMatchKind::exact,
                            InputMatchKind::incomplete_completion,
                            InputMatchKind::corrected,
                            InputMatchKind::abbreviated_completion}) {
        for (const auto& path : parsed.paths) {
            if (path.match_kind == kind) ordered_paths.push_back(&path);
        }
    }
    bool exact_candidate_found = false;
    for (const auto* path_pointer : ordered_paths) {
        const auto& path = *path_pointer;
        // Corrections are a fallback for spellings not covered by the lexicon.
        // Prefix completions remain available beside exact candidates.
        if (path.match_kind == InputMatchKind::corrected && exact_candidate_found) continue;
        if (std::any_of(path.syllables.begin(), path.syllables.end(),
                        [](const Syllable& value) { return !value.complete; })) continue;

        std::vector<std::vector<SearchState>> chart(path.syllables.size() + 1);
        SearchState initial;
        initial.score = -input_match_penalty(path);
        chart[0].push_back(std::move(initial));
        const std::size_t beam_width = std::max<std::size_t>(16, limit * 4);
        for (std::size_t begin = 0; begin < path.syllables.size(); ++begin) {
            if (chart[begin].empty()) continue;
            prune(chart[begin], beam_width);
            for (std::size_t end = begin + 1; end <= path.syllables.size(); ++end) {
                std::vector<std::string_view> reading;
                reading.reserve(end - begin);
                for (std::size_t index = begin; index < end; ++index)
                    reading.push_back(path.syllables[index].text);
                const auto entries = lexicon_.lookup(reading);
                for (const auto& state : chart[begin]) {
                    for (const auto& entry : entries) {
                        SearchState next = state;
                        next.text += entry.text;
                        next.score += unigram_score(entry.frequency);
                        if (!state.previous.empty()) next.score -= kAdditionalSegmentPenalty;
                        if (bigram_ != nullptr && !state.previous.empty())
                            next.score += bigram_->score(state.previous, entry.text);
                        next.previous = entry.text;
                        next.syllables.insert(next.syllables.end(), entry.syllables.begin(),
                                              entry.syllables.end());
                        chart[end].push_back(std::move(next));
                    }
                }
                prune(chart[end], beam_width);
            }
        }

        for (auto& state : chart.back()) {
            if (user_frequency_ != nullptr) state.score += user_frequency_->score(state.text);
            Candidate candidate{std::move(state.text), std::move(state.syllables), state.score,
                                path.match_kind};
            if (path.match_kind == InputMatchKind::exact) exact_candidate_found = true;
            const auto found = unique.find(candidate.text);
            if (found == unique.end() || candidate.score > found->second.score ||
                (candidate.score == found->second.score &&
                 candidate.match_kind < found->second.match_kind)) {
                unique.insert_or_assign(candidate.text, std::move(candidate));
            }
        }
    }

    std::vector<Candidate> candidates;
    candidates.reserve(unique.size());
    for (auto& [text, candidate] : unique) candidates.push_back(std::move(candidate));
    std::sort(candidates.begin(), candidates.end(), [](const Candidate& left, const Candidate& right) {
        if (left.score != right.score) return left.score > right.score;
        if (left.match_kind != right.match_kind) return left.match_kind < right.match_kind;
        if (left.syllables.size() != right.syllables.size())
            return left.syllables.size() < right.syllables.size();
        return left.text < right.text;
    });
    const auto exact = std::find_if(candidates.begin(), candidates.end(), [](const Candidate& candidate) {
        return candidate.match_kind == InputMatchKind::exact;
    });
    if (exact != candidates.end() && exact != candidates.begin())
        std::rotate(candidates.begin(), exact, exact + 1);
    if (candidates.size() > limit) candidates.resize(limit);
    return candidates;
}

}  // namespace owo::engine
