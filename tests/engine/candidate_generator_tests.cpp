#include "owo/engine/candidate_generator.h"
#include "owo/engine/full_pinyin_schema.h"

#include <algorithm>
#include <iostream>
#include <string_view>

namespace {

int fail(const std::string_view message) {
    std::cerr << message << '\n';
    return 1;
}

}  // namespace

int main() {
    // A deliberately small, repository-owned fixture. It validates engine
    // semantics and is not presented as an imported Rime Ice lexicon.
    const owo::engine::MemoryLexicon lexicon({
        {{"ni", "hao"}, "你好", 1000},
        {{"ni", "hao"}, "你号", 50},
        {{"xian"}, "先", 800},
        {{"xian"}, "线", 700},
        {{"xi", "an"}, "西安", 900},
        {{"zhong", "gu"}, "中古", 200},
        {{"zhong", "guo"}, "中国", 2000},
        {{"ba"}, "把", 1200},
        {{"bai"}, "白", 1000},
        {{"niao"}, "鸟", 5000},
        {{"wo", "qu"}, "我去", 1000},
        {{"wei", "qu"}, "委屈", 1200},
        {{"ke", "yi"}, "可以", 1500},
        {{"gou", "mai"}, "购买", 1600},
        {{"gou", "ma"}, "够吗", 1200},
        {{"nian", "hou"}, "年后", 10000},
    });
    const owo::engine::FullPinyinSchema schema;
    const owo::engine::CandidateGenerator generator(lexicon);

    const auto nihao = generator.generate(schema.parse("nihao"));
    if (nihao.size() != 2 || nihao[0].text != "你好" || nihao[1].text != "你号")
        return fail("real Chinese nihao candidates failed");

    const auto xian = generator.generate(schema.parse("xian"));
    if (xian.size() != 3 || xian[0].text != "西安" || xian[1].text != "先" ||
        xian[2].text != "线") return fail("ambiguous reading ranking failed");

    const auto separated = generator.generate(schema.parse("xi'an"));
    if (separated.size() != 1 || separated[0].text != "西安")
        return fail("explicit syllable boundary candidate failed");

    const auto incomplete = generator.generate(schema.parse("zhongg"));
    if (incomplete.empty() || incomplete[0].text != "中国" ||
        incomplete[0].match_kind != owo::engine::InputMatchKind::incomplete_completion)
        return fail("incomplete suffix did not generate a candidate");

    const auto mixed_incomplete = generator.generate(schema.parse("nih"));
    if (mixed_incomplete.empty() || mixed_incomplete[0].text != "你好" ||
        mixed_incomplete[0].match_kind != owo::engine::InputMatchKind::incomplete_completion)
        return fail("complete and incomplete syllables did not generate a candidate");

    const auto single_initial = generator.generate(schema.parse("b"));
    if (single_initial.empty() || single_initial[0].text != "把")
        return fail("single initial did not generate prefix candidates");

    const auto abbreviated = generator.generate(schema.parse("wq"));
    if (abbreviated.empty() || abbreviated[0].text != "我去" ||
        abbreviated[0].match_kind != owo::engine::InputMatchKind::abbreviated_completion)
        return fail("multi-syllable abbreviation did not generate candidates");

    const auto initial_abbreviation = generator.generate(schema.parse("ky"));
    if (initial_abbreviation.empty() || initial_abbreviation[0].text != "可以")
        return fail("initial abbreviation did not generate candidates");

    const auto mixed_abbreviation = generator.generate(schema.parse("gom"));
    if (std::none_of(mixed_abbreviation.begin(), mixed_abbreviation.end(), [](const auto& value) {
            return value.text == "购买";
        }) ||
        std::none_of(mixed_abbreviation.begin(), mixed_abbreviation.end(), [](const auto& value) {
            return value.text == "够吗";
        })) return fail("mixed abbreviation did not generate expected candidates");

    const auto corrected = generator.generate(schema.parse("niaho"));
    if (corrected.empty() ||
        corrected[0].match_kind != owo::engine::InputMatchKind::corrected ||
        std::none_of(corrected.begin(), corrected.end(), [](const auto& value) {
            return value.text == "你好" &&
                   value.match_kind == owo::engine::InputMatchKind::corrected;
        }))
        return fail("transposed input did not generate corrected candidates");

    const auto exact_and_completed = generator.generate(schema.parse("zhonggu"));
    if (exact_and_completed.size() < 2 || exact_and_completed[0].text != "中古" ||
        exact_and_completed[0].match_kind != owo::engine::InputMatchKind::exact ||
        std::none_of(exact_and_completed.begin(), exact_and_completed.end(), [](const auto& value) {
            return value.text == "中国" &&
                   value.match_kind == owo::engine::InputMatchKind::incomplete_completion;
        })) return fail("exact candidate priority or completion candidate failed");

    const auto limited = generator.generate(schema.parse("nihao"), 1);
    if (limited.size() != 1 || limited[0].text != "你好") return fail("candidate limit failed");

    const auto exact_without_correction = generator.generate(schema.parse("nihao"));
    if (std::any_of(exact_without_correction.begin(), exact_without_correction.end(),
                    [](const auto& value) {
                        return value.match_kind == owo::engine::InputMatchKind::corrected;
                    })) return fail("correction was mixed into an exact dictionary match");

    const owo::engine::MemoryLexicon segmentation({
        {{"ni", "hao"}, "你好", 332885},
        {{"ni"}, "你", 20000000}, {{"hao"}, "好", 18000000},
        {{"ha"}, "哈", 16000000}, {{"o"}, "哦", 15000000},
    });
    const owo::engine::CandidateGenerator segmentation_generator(segmentation);
    const auto segmented = segmentation_generator.generate(schema.parse("nihao"));
    if (segmented.empty() || segmented[0].text != "你好")
        return fail("over-segmentation outranked whole word");

    const owo::engine::MemoryLexicon compositional({
        {{"ni"}, "你", 1000}, {{"ni"}, "泥", 950},
        {{"hao"}, "好", 1000}, {{"hao"}, "号", 950},
    });
    owo::engine::MemoryBigramModel bigram;
    bigram.set("你", "好", 5000);
    bigram.set("泥", "号", -5000);
    const owo::engine::CandidateGenerator beam_generator(compositional, &bigram);
    const auto composed = beam_generator.generate(schema.parse("nihao"));
    if (composed.size() != 4 || composed[0].text != "你好" ||
        composed[0].syllables != std::vector<std::string>{"ni", "hao"})
        return fail("beam search or bigram ranking failed");

    owo::engine::UserFrequencyStore user_frequency;
    user_frequency.record("泥号", 20);
    const owo::engine::CandidateGenerator personalized(compositional, nullptr, &user_frequency);
    const auto learned = personalized.generate(schema.parse("nihao"));
    if (learned.empty() || learned[0].text != "泥号")
        return fail("user frequency ranking failed");
    return 0;
}
