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

bool contains_path(const owo::engine::ParseResult& result,
                   const std::initializer_list<std::string_view> expected,
                   const owo::engine::InputMatchKind match_kind) {
    return std::any_of(result.paths.begin(), result.paths.end(), [&](const auto& path) {
        if (path.match_kind != match_kind || path.syllables.size() != expected.size()) return false;
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
    if (!contains_path(incomplete, {"zhong", "guo"},
                       owo::engine::InputMatchKind::incomplete_completion))
        return fail("incomplete suffix completion failed");

    const auto initial = schema.parse("b");
    if (!initial.valid || !initial.has_incomplete_syllable ||
        !contains_path(initial, {"ba"}, owo::engine::InputMatchKind::incomplete_completion) ||
        !contains_path(initial, {"bu"}, owo::engine::InputMatchKind::incomplete_completion))
        return fail("single-letter completion failed");

    if (!contains_path(schema.parse("wq"), {"wo", "qu"},
                       owo::engine::InputMatchKind::abbreviated_completion) ||
        !contains_path(schema.parse("ky"), {"ke", "yi"},
                       owo::engine::InputMatchKind::abbreviated_completion))
        return fail("multi-syllable abbreviation completion failed");
    const auto mixed_abbreviation = schema.parse("gom");
    if (!contains_path(mixed_abbreviation, {"gou", "mai"},
                       owo::engine::InputMatchKind::abbreviated_completion) ||
        !contains_path(mixed_abbreviation, {"gou", "ma"},
                       owo::engine::InputMatchKind::abbreviated_completion))
        return fail("mixed abbreviation completion failed");

    if (!contains_path(schema.parse("niaho"), {"ni", "hao"},
                       owo::engine::InputMatchKind::corrected))
        return fail("transposed-letter correction failed");
    if (contains_path(schema.parse("niaho", 32, false), {"ni", "hao"},
                      owo::engine::InputMatchKind::corrected))
        return fail("disabled correction still produced a corrected path");
    if (!contains_path(schema.parse("nihap"), {"ni", "hao"},
                       owo::engine::InputMatchKind::corrected))
        return fail("adjacent-key correction failed");
    if (!contains_path(schema.parse("niho"), {"ni", "hao"},
                       owo::engine::InputMatchKind::corrected))
        return fail("missing-letter correction failed");
    if (!contains_path(schema.parse("nihhao"), {"ni", "hao"},
                       owo::engine::InputMatchKind::corrected))
        return fail("extra-letter correction failed");
    if (contains_path(schema.parse("nihax"), {"ni", "hao"},
                      owo::engine::InputMatchKind::corrected))
        return fail("non-adjacent substitution was corrected");
    if (contains_path(schema.parse("nxhap"), {"ni", "hao"},
                      owo::engine::InputMatchKind::corrected))
        return fail("two edits were corrected");
    if (!contains_path(schema.parse("zhonggu"), {"zhong", "guo"},
                       owo::engine::InputMatchKind::incomplete_completion))
        return fail("complete-prefix completion failed");

    const auto capped_assisted = schema.parse("nihap", 8);
    if (capped_assisted.paths.size() > 8 ||
        !contains_path(capped_assisted, {"ni", "hao"},
                       owo::engine::InputMatchKind::corrected))
        return fail("assisted path budget starved correction");

    if (schema.parse("ni hao").valid || schema.parse("'ni").valid ||
        schema.parse("ni''hao").valid || schema.parse("").valid)
        return fail("invalid input was accepted");

    const auto limited = schema.parse("xian", 1);
    if (!limited.valid || limited.paths.size() != 1) return fail("path cap failed");

    std::string long_input;
    for (int index = 0; index < 80; ++index) {
        if (!long_input.empty()) long_input.push_back('\'');
        long_input += "ni";
    }
    const auto long_result = schema.parse(long_input);
    if (!long_result.valid || long_result.paths.empty() ||
        long_result.paths.front().syllables.size() != 80)
        return fail("long pinyin input was truncated");

    const auto long_initials = schema.parse("ffffffffff");
    if (!long_initials.valid || !long_initials.has_incomplete_syllable ||
        std::none_of(long_initials.paths.begin(), long_initials.paths.end(), [](const auto& path) {
            return path.syllables.size() == 10 &&
                   std::all_of(path.syllables.begin(), path.syllables.end(),
                               [](const auto& syllable) { return syllable.text == "f"; });
        })) return fail("long initial sequence was not segmented");

    const auto separated_initials = schema.parse("f'f'f'f'f'f'f'f'f'f");
    if (!separated_initials.valid || separated_initials.paths.empty())
        return fail("separated incomplete initials were rejected");

    return 0;
}
