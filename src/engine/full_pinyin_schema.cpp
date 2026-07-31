#include "owo/engine/full_pinyin_schema.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <functional>
#include <string_view>

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

std::vector<ChunkPath> parse_chunk(const std::string_view normalized,
                                   const std::size_t begin,
                                   const std::size_t end,
                                   const std::size_t max_paths) {
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
        if (paths.size() < max_paths && is_prefix(suffix)) {
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
    std::size_t chunk_begin = 0;
    while (chunk_begin < result.normalized_input.size()) {
        const auto separator = result.normalized_input.find('\'', chunk_begin);
        const auto chunk_end = separator == std::string::npos ? result.normalized_input.size() : separator;
        const auto chunk_paths = parse_chunk(result.normalized_input, chunk_begin, chunk_end, max_paths);
        if (chunk_paths.empty()) return result;

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

    result.paths = std::move(combined);
    result.valid = !result.paths.empty();
    result.has_incomplete_syllable = any_incomplete;
    return result;
}

}  // namespace owo::engine
