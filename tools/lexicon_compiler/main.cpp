#include "owo/engine/binary_lexicon.h"

#include <charconv>
#include <fstream>
#include <iostream>
#include <map>
#include <sstream>
#include <string>
#include <unordered_set>

namespace {

std::string trim_cr(std::string value) {
    if (!value.empty() && value.back() == '\r') value.pop_back();
    return value;
}

bool valid_sha256(const std::string& value) {
    if (value.size() != 64) return false;
    return value.find_first_not_of("0123456789abcdefABCDEF") == std::string::npos;
}

bool read_manifest(const char* path) {
    std::ifstream input(path);
    if (!input) return false;
    std::map<std::string, std::string> fields;
    std::string line;
    while (std::getline(input, line)) {
        line = trim_cr(std::move(line));
        if (line.empty() || line.front() == '#') continue;
        const auto separator = line.find('=');
        if (separator == std::string::npos || separator == 0 || separator + 1 == line.size()) return false;
        if (!fields.emplace(line.substr(0, separator), line.substr(separator + 1)).second) return false;
    }
    return fields.size() == 4 && fields.contains("source_id") &&
           fields.contains("source_version") && fields.contains("source_sha256") &&
           fields.contains("license") && valid_sha256(fields["source_sha256"]);
}

bool parse_entries(const char* path, std::vector<owo::engine::LexiconEntry>& entries) {
    std::ifstream input(path, std::ios::binary);
    if (!input) return false;
    std::unordered_set<std::string> unique;
    std::string line;
    std::size_t line_number = 0;
    while (std::getline(input, line)) {
        ++line_number;
        line = trim_cr(std::move(line));
        if (line.empty() || line.front() == '#') continue;
        const auto first = line.find('\t');
        const auto second = first == std::string::npos ? first : line.find('\t', first + 1);
        if (first == std::string::npos || second == std::string::npos ||
            line.find('\t', second + 1) != std::string::npos) {
            std::cerr << "invalid TSV at line " << line_number << '\n';
            return false;
        }
        owo::engine::LexiconEntry entry;
        entry.text = line.substr(0, first);
        std::istringstream reading(line.substr(first + 1, second - first - 1));
        for (std::string syllable; reading >> syllable;) {
            if (syllable.find_first_not_of("abcdefghijklmnopqrstuvwxyz") != std::string::npos) return false;
            entry.syllables.push_back(std::move(syllable));
        }
        const auto frequency_text = std::string_view(line).substr(second + 1);
        const auto result = std::from_chars(frequency_text.data(),
                                            frequency_text.data() + frequency_text.size(),
                                            entry.frequency);
        if (entry.text.empty() || entry.syllables.empty() || result.ec != std::errc{} ||
            result.ptr != frequency_text.data() + frequency_text.size()) return false;
        std::string key;
        for (const auto& syllable : entry.syllables) key += syllable + '\0';
        key += entry.text;
        if (!unique.insert(std::move(key)).second) {
            std::cerr << "duplicate entry at line " << line_number << '\n';
            return false;
        }
        entries.push_back(std::move(entry));
    }
    return !entries.empty();
}

}  // namespace

int main(int argc, char** argv) {
    if (argc != 4) {
        std::cerr << "usage: owo_lexicon_compiler <source.tsv> <source.manifest> <output.owolx>\n";
        return 2;
    }
    if (!read_manifest(argv[2])) {
        std::cerr << "invalid source manifest\n";
        return 3;
    }
    std::vector<owo::engine::LexiconEntry> entries;
    if (!parse_entries(argv[1], entries)) return 4;
    const auto result = owo::engine::write_binary_lexicon(argv[3], std::move(entries));
    if (!result.success) {
        std::cerr << result.error << '\n';
        return 5;
    }
    return 0;
}
