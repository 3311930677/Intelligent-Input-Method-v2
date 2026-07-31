#include "owo/engine/binary_lexicon.h"

#include <filesystem>
#include <fstream>
#include <iostream>

int main() {
    const auto first = std::filesystem::temp_directory_path() / "owo-lexicon-test-1.bin";
    const auto second = std::filesystem::temp_directory_path() / "owo-lexicon-test-2.bin";
    const std::vector<owo::engine::LexiconEntry> entries{
        {{"xi", "an"}, "西安", 900}, {{"ni", "hao"}, "你好", 1000},
        {{"ni", "hao"}, "你号", 50}};
    if (!owo::engine::write_binary_lexicon(first, entries).success ||
        !owo::engine::write_binary_lexicon(second, {entries.rbegin(), entries.rend()}).success) return 1;

    std::ifstream left(first, std::ios::binary), right(second, std::ios::binary);
    const std::string left_bytes((std::istreambuf_iterator<char>(left)), {});
    const std::string right_bytes((std::istreambuf_iterator<char>(right)), {});
    if (left_bytes != right_bytes) { std::cerr << "output is not deterministic\n"; return 1; }

    owo::engine::BinaryLexicon lexicon;
    if (!lexicon.load(first).success || lexicon.size() != 3) return 1;
    const std::string_view reading[]{"ni", "hao"};
    const auto matches = lexicon.lookup(reading);
    if (matches.size() != 2 || matches[0].text != "你号" || matches[1].text != "你好") return 1;

    auto corrupted = left_bytes;
    corrupted.back() ^= 1;
    { std::ofstream output(second, std::ios::binary | std::ios::trunc); output.write(corrupted.data(), static_cast<std::streamsize>(corrupted.size())); }
    owo::engine::BinaryLexicon rejected;
    if (rejected.load(second).success) { std::cerr << "corruption was accepted\n"; return 1; }
    std::error_code ignored;
    std::filesystem::remove(first, ignored);
    std::filesystem::remove(second, ignored);
    return 0;
}
