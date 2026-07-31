#pragma once

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace owo::engine {

struct Syllable {
    std::string text;
    std::size_t begin{};
    std::size_t end{};
    bool complete{true};

    bool operator==(const Syllable&) const = default;
};

struct ParsePath {
    std::vector<Syllable> syllables;

    bool operator==(const ParsePath&) const = default;
};

struct ParseResult {
    std::string normalized_input;
    std::vector<ParsePath> paths;
    bool valid{false};
    bool has_incomplete_syllable{false};
};

class InputSchema {
public:
    virtual ~InputSchema() = default;
    [[nodiscard]] virtual ParseResult parse(std::string_view input,
                                            std::size_t max_paths = 32) const = 0;
};

}  // namespace owo::engine
