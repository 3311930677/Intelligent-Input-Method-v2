#include "owo/engine/binary_lexicon.h"

#ifdef _WIN32
#include <Windows.h>
#include <bcrypt.h>
#endif

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

bool read_manifest(const char* path, std::string& source_sha256) {
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
    const bool valid = fields.size() == 4 && fields.contains("source_id") &&
           fields.contains("source_version") && fields.contains("source_sha256") &&
           fields.contains("license") && valid_sha256(fields["source_sha256"]);
    if (valid) source_sha256 = fields["source_sha256"];
    return valid;
}

std::string sha256_file(const char* path) {
#ifdef _WIN32
    std::ifstream input(path, std::ios::binary);
    if (!input) return {};
    std::vector<unsigned char> bytes((std::istreambuf_iterator<char>(input)), {});
    BCRYPT_ALG_HANDLE algorithm = nullptr;
    BCRYPT_HASH_HANDLE hash = nullptr;
    DWORD hash_size = 0, result_size = 0;
    if (BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0) < 0 ||
        BCryptGetProperty(algorithm, BCRYPT_HASH_LENGTH,
                          reinterpret_cast<PUCHAR>(&hash_size), sizeof(hash_size),
                          &result_size, 0) < 0 ||
        BCryptCreateHash(algorithm, &hash, nullptr, 0, nullptr, 0, 0) < 0 ||
        BCryptHashData(hash, bytes.data(), static_cast<ULONG>(bytes.size()), 0) < 0) {
        if (hash != nullptr) BCryptDestroyHash(hash);
        if (algorithm != nullptr) BCryptCloseAlgorithmProvider(algorithm, 0);
        return {};
    }
    std::vector<unsigned char> digest(hash_size);
    const bool finished = BCryptFinishHash(hash, digest.data(), hash_size, 0) >= 0;
    BCryptDestroyHash(hash);
    BCryptCloseAlgorithmProvider(algorithm, 0);
    if (!finished) return {};
    constexpr char hex[] = "0123456789abcdef";
    std::string output;
    output.reserve(digest.size() * 2);
    for (const auto byte : digest) {
        output.push_back(hex[byte >> 4U]);
        output.push_back(hex[byte & 0x0fU]);
    }
    return output;
#else
    static_cast<void>(path);
    return {};
#endif
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
    std::string expected_sha256;
    if (!read_manifest(argv[2], expected_sha256)) {
        std::cerr << "invalid source manifest\n";
        return 3;
    }
    if (sha256_file(argv[1]) != expected_sha256) {
        std::cerr << "source SHA-256 does not match manifest\n";
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
