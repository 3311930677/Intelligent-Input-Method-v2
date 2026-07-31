#pragma once

#include "owo/engine/input_schema.h"
#include "owo/engine/lexicon.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace owo::engine {

struct Candidate {
    std::string text;
    std::vector<std::string> syllables;
    std::int64_t score{};

    bool operator==(const Candidate&) const = default;
};

class CandidateGenerator {
public:
    explicit CandidateGenerator(const Lexicon& lexicon) : lexicon_(lexicon) {}
    [[nodiscard]] std::vector<Candidate> generate(const ParseResult& parsed,
                                                  std::size_t limit = 32) const;

private:
    const Lexicon& lexicon_;
};

}  // namespace owo::engine
