#include "owo/engine/candidate_generator.h"
#include "owo/engine/full_pinyin_schema.h"

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

    if (!generator.generate(schema.parse("zhongg")).empty())
        return fail("incomplete syllable generated a candidate");

    const auto limited = generator.generate(schema.parse("nihao"), 1);
    if (limited.size() != 1 || limited[0].text != "你好") return fail("candidate limit failed");
    return 0;
}
