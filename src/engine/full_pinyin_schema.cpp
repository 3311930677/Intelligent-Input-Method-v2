#include "owo/engine/full_pinyin_schema.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdlib>
#include <functional>
#include <iterator>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_set>

namespace owo::engine {
namespace {

// Canonical tone-less Hanyu Pinyin syllables. Interjections and common
// orthographic forms used by dictionaries are included; tone digits are not.
constexpr std::string_view kSyllables[] = {
    "a", "ai", "an", "ang", "ao", "ba", "bai", "ban", "bang", "bao", "bei", "ben", "beng", "bi", "bian", "biao", "bie", "bin", "bing", "bo", "bu",
    "ca", "cai", "can", "cang", "cao", "ce", "cen", "ceng", "cha", "chai", "chan", "chang", "chao", "che", "chen", "cheng", "chi", "chong", "chou", "chu", "chua", "chuai", "chuan", "chuang", "chui", "chun", "chuo", "ci", "cong", "cou", "cu", "cuan", "cui", "cun", "cuo",
    "da", "dai", "dan", "dang", "dao", "de", "dei", "den", "deng", "di", "dia", "dian", "diao", "die", "ding", "diu", "dong", "dou", "du", "duan", "dui", "dun", "duo",
    "e", "ei", "en", "eng", "er",
    "fa", "fan", "fang", "fei", "fen", "feng", "fiao", "fo", "fou", "fu",
    "ga", "gai", "gan", "gang", "gao", "ge", "gei", "gen", "geng", "gong", "gou", "gu", "gua", "guai", "guan", "guang", "gui", "gun", "guo",
    "ha", "hai", "han", "hang", "hao", "he", "hei", "hen", "heng", "hm", "hng", "hong", "hou", "hu", "hua", "huai", "huan", "huang", "hui", "hun", "huo",
    "ji", "jia", "jian", "jiang", "jiao", "jie", "jin", "jing", "jiong", "jiu", "ju", "juan", "jue", "jun",
    "ka", "kai", "kan", "kang", "kao", "ke", "kei", "ken", "keng", "kong", "kou", "ku", "kua", "kuai", "kuan", "kuang", "kui", "kun", "kuo",
    "la", "lai", "lan", "lang", "lao", "le", "lei", "leng", "li", "lia", "lian", "liang", "liao", "lie", "lin", "ling", "liu", "lo", "long", "lou", "lu", "luan", "lun", "luo", "lv", "lve",
    "m", "ma", "mai", "man", "mang", "mao", "me", "mei", "men", "meng", "mi", "mian", "miao", "mie", "min", "ming", "miu", "mo", "mou", "mu",
    "n", "na", "nai", "nan", "nang", "nao", "ne", "nei", "nen", "neng", "ng", "ni", "nian", "niang", "niao", "nie", "nin", "ning", "niu", "nong", "nou", "nu", "nuan", "nun", "nuo", "nv", "nve",
    "o", "ou",
    "pa", "pai", "pan", "pang", "pao", "pei", "pen", "peng", "pi", "pian", "piao", "pie", "pin", "ping", "po", "pou", "pu",
    "qi", "qia", "qian", "qiang", "qiao", "qie", "qin", "qing", "qiong", "qiu", "qu", "quan", "que", "qun",
    "ran", "rang", "rao", "re", "ren", "reng", "ri", "rong", "rou", "ru", "rua", "ruan", "rui", "run", "ruo",
    "sa", "sai", "san", "sang", "sao", "se", "sen", "seng", "sha", "shai", "shan", "shang", "shao", "she", "shei", "shen", "sheng", "shi", "shou", "shu", "shua", "shuai", "shuan", "shuang", "shui", "shun", "shuo", "si", "song", "sou", "su", "suan", "sui", "sun", "suo",
    "ta", "tai", "tan", "tang", "tao", "te", "teng", "ti", "tian", "tiao", "tie", "ting", "tong", "tou", "tu", "tuan", "tui", "tun", "tuo",
    "wa", "wai", "wan", "wang", "wei", "wen", "weng", "wo", "wu",
    "xi", "xia", "xian", "xiang", "xiao", "xie", "xin", "xing", "xiong", "xiu", "xu", "xuan", "xue", "xun",
    "ya", "yan", "yang", "yao", "ye", "yi", "yin", "ying", "yo", "yong", "you", "yu", "yuan", "yue", "yun",
    "za", "zai", "zan", "zang", "zao", "ze", "zei", "zen", "zeng", "zha", "zhai", "zhan", "zhang", "zhao", "zhe", "zhei", "zhen", "zheng", "zhi", "zhong", "zhou", "zhu", "zhua", "zhuai", "zhuan", "zhuang", "zhui", "zhun", "zhuo", "zi", "zong", "zou", "zu", "zuan", "zui", "zun", "zuo"};

bool is_complete(const std::string_view value) {
    return std::find(std::begin(kSyllables), std::end(kSyllables), value) != std::end(kSyllables);
}

bool is_prefix(const std::string_view value) {
    return std::any_of(std::begin(kSyllables), std::end(kSyllables),
                       [value](const std::string_view syllable) {
                           return syllable.size() > value.size() && syllable.starts_with(value);
                       });
}

struct ChunkPath {
    std::vector<Syllable> syllables;
    bool incomplete{};
};

bool assisted_path_less(const ParsePath& left, const ParsePath& right) {
    if (left.syllables.size() != right.syllables.size())
        return left.syllables.size() < right.syllables.size();
    if (left.completion_characters != right.completion_characters)
        return left.completion_characters < right.completion_characters;
    return std::lexicographical_compare(
        left.syllables.begin(), left.syllables.end(), right.syllables.begin(), right.syllables.end(),
        [](const Syllable& first, const Syllable& second) { return first.text < second.text; });
}

std::string assisted_reading_key(const ParsePath& path) {
    std::string key(1, static_cast<char>(path.match_kind));
    for (const auto& syllable : path.syllables) {
        key += syllable.text;
        key.push_back('\'');
    }
    return key;
}

struct KeyboardPosition {
    int row{};
    int column{};
};

std::optional<KeyboardPosition> keyboard_position(const char key) {
    constexpr std::string_view rows[]{"qwertyuiop", "asdfghjkl", "zxcvbnm"};
    for (int row = 0; row < 3; ++row) {
        const auto column = rows[row].find(key);
        if (column != std::string_view::npos)
            return KeyboardPosition{row, static_cast<int>(column) * 2 + row};
    }
    return std::nullopt;
}

bool substitution_allowed(const char expected, const char actual) {
    if ((expected == 'l' && actual == 'n') || (expected == 'n' && actual == 'l') ||
        (expected == 'f' && actual == 'h') || (expected == 'h' && actual == 'f')) return true;
    const auto left = keyboard_position(expected);
    const auto right = keyboard_position(actual);
    return left && right && std::abs(left->row - right->row) <= 1 &&
           std::abs(left->column - right->column) <= 2;
}

enum class EditRelation { none, exact, corrected };

EditRelation one_edit_relation(const std::string_view expected,
                               const std::string_view actual) {
    if (expected == actual) return EditRelation::exact;
    if (expected.size() == actual.size()) {
        std::array<std::size_t, 2> mismatch{};
        std::size_t count = 0;
        for (std::size_t index = 0; index < expected.size(); ++index) {
            if (expected[index] == actual[index]) continue;
            if (count == mismatch.size()) return EditRelation::none;
            mismatch[count++] = index;
        }
        if (count == 1 && substitution_allowed(expected[mismatch[0]], actual[mismatch[0]]))
            return EditRelation::corrected;
        if (count == 2 && mismatch[1] == mismatch[0] + 1 &&
            expected[mismatch[0]] == actual[mismatch[1]] &&
            expected[mismatch[1]] == actual[mismatch[0]]) return EditRelation::corrected;
        return EditRelation::none;
    }
    if (expected.size() + 1 != actual.size() && actual.size() + 1 != expected.size())
        return EditRelation::none;
    const auto longer = expected.size() > actual.size() ? expected : actual;
    const auto shorter = expected.size() > actual.size() ? actual : expected;
    std::size_t long_index = 0;
    std::size_t short_index = 0;
    bool skipped = false;
    while (long_index < longer.size() && short_index < shorter.size()) {
        if (longer[long_index] == shorter[short_index]) {
            ++long_index;
            ++short_index;
        } else if (!skipped) {
            skipped = true;
            ++long_index;
        } else {
            return EditRelation::none;
        }
    }
    return EditRelation::corrected;
}

void append_incomplete_completions(const std::vector<ParsePath>& base_paths,
                                   std::vector<ParsePath>& paths,
                                   const std::size_t max_paths,
                                   const std::size_t inclusive_syllable_limit) {
    std::vector<ParsePath> completions;
    for (const auto& base : base_paths) {
        if (completions.size() >= max_paths) break;
        if (base.syllables.size() > inclusive_syllable_limit) continue;
        std::vector<std::size_t> incomplete;
        for (std::size_t index = 0; index < base.syllables.size(); ++index) {
            if (!base.syllables[index].complete) incomplete.push_back(index);
        }
        if (incomplete.empty()) continue;
        ParsePath current = base;
        current.match_kind = InputMatchKind::incomplete_completion;
        current.completion_characters = 0;
        std::function<void(std::size_t)> expand = [&](const std::size_t position) {
            if (completions.size() >= max_paths) return;
            if (position == incomplete.size()) {
                completions.push_back(current);
                return;
            }
            const auto index = incomplete[position];
            const auto prefix = base.syllables[index].text;
            for (const auto syllable : kSyllables) {
                if (syllable.size() <= prefix.size() || !syllable.starts_with(prefix)) continue;
                current.syllables[index].text = std::string(syllable);
                current.syllables[index].complete = true;
                current.completion_characters +=
                    static_cast<std::uint32_t>(syllable.size() - prefix.size());
                expand(position + 1);
                current.completion_characters -=
                    static_cast<std::uint32_t>(syllable.size() - prefix.size());
                if (completions.size() >= max_paths) break;
            }
            current.syllables[index] = base.syllables[index];
        };
        expand(0);
    }
    std::sort(completions.begin(), completions.end(), assisted_path_less);
    for (auto& completion : completions) {
        if (paths.size() >= max_paths) break;
        paths.push_back(std::move(completion));
    }
}

void prune_assisted_paths(std::vector<ParsePath>& paths, const std::size_t limit,
                          const bool deduplicate = false) {
    std::sort(paths.begin(), paths.end(), assisted_path_less);
    if (!deduplicate) {
        if (paths.size() > limit) paths.resize(limit);
        return;
    }
    std::vector<ParsePath> unique;
    unique.reserve(std::min(paths.size(), limit));
    std::unordered_set<std::string> seen;
    seen.reserve(std::min(paths.size(), limit) * 2);
    for (auto& path : paths) {
        if (!seen.insert(assisted_reading_key(path)).second) continue;
        unique.push_back(std::move(path));
        if (unique.size() >= limit) break;
    }
    paths = std::move(unique);
}

void append_abbreviated_completions(const std::string_view normalized,
                                    std::vector<ParsePath>& paths,
                                    const std::size_t max_paths) {
    constexpr std::size_t kMinimumAbbreviatedInputBytes = 2;
    constexpr std::size_t kMaximumAbbreviatedInputBytes = 16;
    constexpr std::size_t kMaximumAbbreviatedSyllables = 8;
    if (normalized.size() < kMinimumAbbreviatedInputBytes ||
        normalized.size() > kMaximumAbbreviatedInputBytes ||
        normalized.find('\'') != std::string_view::npos || max_paths == 0) return;

    const auto beam_width = std::max<std::size_t>(32, std::min<std::size_t>(32, max_paths) * 4);
    std::vector<std::vector<ParsePath>> chart(normalized.size() + 1);
    ParsePath initial;
    initial.match_kind = InputMatchKind::abbreviated_completion;
    chart[0].push_back(std::move(initial));

    for (std::size_t offset = 0; offset < normalized.size(); ++offset) {
        if (chart[offset].empty()) continue;
        prune_assisted_paths(chart[offset], beam_width);
        for (const auto& state : chart[offset]) {
            if (state.syllables.size() >= kMaximumAbbreviatedSyllables) continue;
            const auto maximum = std::min<std::size_t>(6, normalized.size() - offset);
            for (std::size_t consumed = maximum; consumed > 0; --consumed) {
                const auto prefix = normalized.substr(offset, consumed);
                for (const auto syllable : kSyllables) {
                    if (syllable.size() < prefix.size() || !syllable.starts_with(prefix)) continue;
                    ParsePath next = state;
                    next.syllables.push_back({std::string(syllable), offset,
                                              offset + consumed, true});
                    next.completion_characters +=
                        static_cast<std::uint32_t>(syllable.size() - prefix.size());
                    auto& destination = chart[offset + consumed];
                    destination.push_back(std::move(next));
                    if (destination.size() >= beam_width * 4)
                        prune_assisted_paths(destination, beam_width);
                }
            }
        }
    }

    auto& completed = chart.back();
    completed.erase(std::remove_if(completed.begin(), completed.end(), [](const ParsePath& path) {
        return path.syllables.size() < 2 || path.completion_characters == 0;
    }), completed.end());
    prune_assisted_paths(completed, max_paths, true);
    for (auto& path : completed) paths.push_back(std::move(path));
}

struct FuzzyToken {
    std::string_view syllable;
    std::size_t consumed{};
    bool corrected{};
};

std::vector<FuzzyToken> fuzzy_tokens(const std::string_view input,
                                     const std::size_t offset) {
    std::vector<FuzzyToken> matches;
    const auto remaining = input.size() - offset;
    for (const auto syllable : kSyllables) {
        const auto minimum = syllable.size() > 1 ? syllable.size() - 1 : 1;
        const auto maximum = syllable.size() + 1;
        for (std::size_t consumed = minimum; consumed <= maximum && consumed <= remaining;
             ++consumed) {
            const auto relation = one_edit_relation(syllable, input.substr(offset, consumed));
            if (relation == EditRelation::none) continue;
            matches.push_back({syllable, consumed, relation == EditRelation::corrected});
        }
    }
    std::sort(matches.begin(), matches.end(), [](const FuzzyToken& left, const FuzzyToken& right) {
        // The exact parser already preserves every unmodified path. Within the
        // correction search, consuming more source text first avoids spending
        // the bounded path budget on variants that keep a stray tail syllable.
        if (left.consumed != right.consumed) return left.consumed > right.consumed;
        if (left.corrected != right.corrected) return !left.corrected;
        return left.syllable < right.syllable;
    });
    matches.erase(std::unique(matches.begin(), matches.end(), [](const auto& left, const auto& right) {
        return left.syllable == right.syllable && left.consumed == right.consumed &&
               left.corrected == right.corrected;
    }), matches.end());
    return matches;
}

void append_corrected_paths(const std::string_view normalized,
                            std::vector<ParsePath>& paths,
                            const std::size_t max_paths,
                            const std::size_t exclusive_syllable_limit) {
    constexpr std::size_t kMaximumCorrectedInputBytes = 32;
    constexpr std::size_t kMinimumCorrectedInputBytes = 3;
    if (normalized.size() < kMinimumCorrectedInputBytes ||
        normalized.size() > kMaximumCorrectedInputBytes ||
        normalized.find('\'') != std::string_view::npos || paths.size() >= max_paths ||
        exclusive_syllable_limit <= 1) return;
    ParsePath current;
    current.match_kind = InputMatchKind::corrected;
    current.edit_count = 1;
    std::function<void(std::size_t, bool)> visit = [&](const std::size_t offset,
                                                       const bool already_corrected) {
        if (paths.size() >= max_paths) return;
        if (offset == normalized.size()) {
            if (already_corrected && current.syllables.size() < exclusive_syllable_limit)
                paths.push_back(current);
            return;
        }
        if (current.syllables.size() + 1 >= exclusive_syllable_limit) return;
        for (const auto& token : fuzzy_tokens(normalized, offset)) {
            if (already_corrected && token.corrected) continue;
            current.syllables.push_back({std::string(token.syllable), offset,
                                         offset + token.consumed, true});
            visit(offset + token.consumed, already_corrected || token.corrected);
            current.syllables.pop_back();
            if (paths.size() >= max_paths) break;
        }
    };
    visit(0, false);
}

std::vector<ChunkPath> parse_chunk(const std::string_view normalized,
                                   const std::size_t begin,
                                   const std::size_t end,
                                   const std::size_t max_paths,
                                   const bool allow_incomplete) {
    std::vector<ChunkPath> paths;
    std::vector<Syllable> current;
    std::function<void(std::size_t)> visit = [&](const std::size_t offset) {
        if (paths.size() >= max_paths) return;
        if (offset == end) {
            paths.push_back({current, false});
            return;
        }

        const auto remaining = end - offset;
        const auto maximum = std::min<std::size_t>(6, remaining);
        for (std::size_t length = maximum; length > 0; --length) {
            const auto token = normalized.substr(offset, length);
            if (is_complete(token)) {
                current.push_back({std::string(token), offset, offset + length, true});
                visit(offset + length);
                current.pop_back();
            }
        }

        // An unfinished syllable is useful only at the end of the whole chunk.
        const auto suffix = normalized.substr(offset, remaining);
        if (allow_incomplete && paths.size() < max_paths && is_prefix(suffix)) {
            current.push_back({std::string(suffix), offset, end, false});
            paths.push_back({current, true});
            current.pop_back();
        }
    };
    visit(begin);
    return paths;
}

}  // namespace

ParseResult FullPinyinSchema::parse(const std::string_view input,
                                    const std::size_t max_paths) const {
    ParseResult result;
    if (input.empty() || max_paths == 0) return result;

    result.normalized_input.reserve(input.size());
    for (const unsigned char character : input) {
        if (character == '\'' || (character >= 'a' && character <= 'z')) {
            result.normalized_input.push_back(static_cast<char>(character));
        } else if (character >= 'A' && character <= 'Z') {
            result.normalized_input.push_back(static_cast<char>(std::tolower(character)));
        } else {
            return result;
        }
    }
    if (result.normalized_input.front() == '\'' || result.normalized_input.back() == '\'' ||
        result.normalized_input.find("''") != std::string::npos) return result;

    std::vector<ParsePath> combined(1);
    bool any_incomplete = false;
    bool base_valid = true;
    std::size_t chunk_begin = 0;
    while (chunk_begin < result.normalized_input.size()) {
        const auto separator = result.normalized_input.find('\'', chunk_begin);
        const auto chunk_end = separator == std::string::npos ? result.normalized_input.size() : separator;
        const auto chunk_paths = parse_chunk(result.normalized_input, chunk_begin, chunk_end,
                                             max_paths, separator == std::string::npos);
        if (chunk_paths.empty()) {
            base_valid = false;
            combined.clear();
            break;
        }

        std::vector<ParsePath> next;
        for (const auto& prefix : combined) {
            for (const auto& suffix : chunk_paths) {
                if (next.size() >= max_paths) break;
                ParsePath path = prefix;
                path.syllables.insert(path.syllables.end(), suffix.syllables.begin(), suffix.syllables.end());
                next.push_back(std::move(path));
                any_incomplete = any_incomplete || suffix.incomplete;
            }
            if (next.size() >= max_paths) break;
        }
        combined = std::move(next);
        if (separator == std::string::npos) break;
        chunk_begin = separator + 1;
    }

    if (base_valid) result.paths = std::move(combined);
    const auto base_paths = result.paths;
    std::size_t corrected_syllable_limit = std::numeric_limits<std::size_t>::max();
    for (const auto& path : base_paths) {
        if (std::any_of(path.syllables.begin(), path.syllables.end(),
                        [](const Syllable& syllable) { return !syllable.complete; })) continue;
        corrected_syllable_limit = std::min(corrected_syllable_limit, path.syllables.size());
    }
    std::vector<ParsePath> incomplete_paths;
    append_incomplete_completions(base_paths, incomplete_paths, max_paths,
                                  corrected_syllable_limit);
    if (corrected_syllable_limit != std::numeric_limits<std::size_t>::max()) {
        for (auto& path : incomplete_paths) {
            if (result.paths.size() >= max_paths) break;
            result.paths.push_back(std::move(path));
        }
    } else {
        std::vector<ParsePath> abbreviated_paths;
        append_abbreviated_completions(result.normalized_input, abbreviated_paths, max_paths);
        incomplete_paths.insert(incomplete_paths.end(),
                                std::make_move_iterator(abbreviated_paths.begin()),
                                std::make_move_iterator(abbreviated_paths.end()));
        prune_assisted_paths(incomplete_paths, max_paths, true);

        std::vector<ParsePath> corrected_paths;
        append_corrected_paths(result.normalized_input, corrected_paths, max_paths,
                               corrected_syllable_limit);

        // Raw incomplete paths cannot produce candidates. Retain one for
        // diagnostics, then reserve a bounded correction slice so a broad
        // one-letter suffix expansion cannot starve useful typo recovery.
        result.paths.clear();
        if (!base_paths.empty()) result.paths.push_back(base_paths.front());
        const auto correction_reserve = std::min<std::size_t>(
            8, std::min(corrected_paths.size(), max_paths - result.paths.size()));
        std::size_t incomplete_index = 0;
        while (incomplete_index < incomplete_paths.size() &&
               result.paths.size() + correction_reserve < max_paths) {
            result.paths.push_back(std::move(incomplete_paths[incomplete_index++]));
        }
        std::size_t corrected_index = 0;
        while (corrected_index < corrected_paths.size() && result.paths.size() < max_paths) {
            result.paths.push_back(std::move(corrected_paths[corrected_index++]));
        }
        while (incomplete_index < incomplete_paths.size() && result.paths.size() < max_paths) {
            result.paths.push_back(std::move(incomplete_paths[incomplete_index++]));
        }
        for (std::size_t index = 1; index < base_paths.size() && result.paths.size() < max_paths;
             ++index) {
            result.paths.push_back(base_paths[index]);
        }
    }
    result.valid = !result.paths.empty();
    result.has_incomplete_syllable = any_incomplete;
    return result;
}

}  // namespace owo::engine
