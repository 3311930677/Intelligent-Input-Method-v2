#include "owo/engine/full_pinyin_schema.h"

#include <algorithm>
#include <iostream>
#include <string_view>

namespace {

bool contains_path(const owo::engine::ParseResult& result,
                   const std::initializer_list<std::string_view> expected) {
    return std::any_of(result.paths.begin(), result.paths.end(), [&](const auto& path) {
        if (path.syllables.size() != expected.size()) return false;
        return std::equal(path.syllables.begin(), path.syllables.end(), expected.begin(), expected.end(),
                          [](const auto& syllable, const auto text) { return syllable.text == text; });
    });
}

int fail(const std::string_view message) {
    std::cerr << message << '\n';
    return 1;
}

}  // namespace

int main() {
    const owo::engine::FullPinyinSchema schema;

    const auto nihao = schema.parse("NiHao");
    if (!nihao.valid || nihao.normalized_input != "nihao" ||
        !contains_path(nihao, {"ni", "hao"})) return fail("nihao parse failed");

    const auto ambiguous = schema.parse("xian");
    if (!contains_path(ambiguous, {"xian"}) || !contains_path(ambiguous, {"xi", "an"}))
        return fail("xian ambiguity was lost");

    const auto separated = schema.parse("xi'an");
    if (!separated.valid || !contains_path(separated, {"xi", "an"}) ||
        contains_path(separated, {"xian"})) return fail("apostrophe boundary failed");

    const auto incomplete = schema.parse("zhongg");
    if (!incomplete.valid || !incomplete.has_incomplete_syllable ||
        !contains_path(incomplete, {"zhong", "g"})) return fail("incomplete suffix failed");

    if (schema.parse("ni hao").valid || schema.parse("'ni").valid ||
        schema.parse("ni''hao").valid || schema.parse("").valid)
        return fail("invalid input was accepted");

    const auto limited = schema.parse("xian", 1);
    if (!limited.valid || limited.paths.size() != 1) return fail("path cap failed");

    return 0;
}
