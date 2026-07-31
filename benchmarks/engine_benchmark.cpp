#include "owo/engine/binary_lexicon.h"
#include "owo/engine/candidate_generator.h"
#include "owo/engine/full_pinyin_schema.h"

#ifdef _WIN32
#include <Windows.h>
#endif

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

namespace {

double percentile(const std::vector<double>& sorted, const double fraction) {
    const auto index = static_cast<std::size_t>(fraction * static_cast<double>(sorted.size() - 1));
    return sorted[index];
}

}  // namespace

int main(int argc, char** argv) {
    if (argc > 2) return 2;
    const bool external_lexicon = argc == 2;
    std::vector<owo::engine::LexiconEntry> entries{
        {{"ni", "hao"}, "你好", 1000}, {{"ni", "hao"}, "你号", 50},
        {{"xian"}, "先", 800}, {{"xian"}, "线", 700}, {{"xi", "an"}, "西安", 900},
        {{"ce", "shi"}, "测试", 600}};
    constexpr std::size_t noise_entries = 20'000;
    entries.reserve(entries.size() + noise_entries);
    for (std::size_t index = 0; index < noise_entries; ++index) {
        entries.push_back({{"synthetic", std::to_string(index)},
                           "synthetic-" + std::to_string(index), 1});
    }

    const auto path = external_lexicon
        ? std::filesystem::path(argv[1])
        : std::filesystem::temp_directory_path() / "owo-engine-benchmark.owolx";
    const auto written = external_lexicon
        ? owo::engine::LexiconIoResult{true, {}}
        : owo::engine::write_binary_lexicon(path, std::move(entries));
    owo::engine::BinaryLexicon lexicon;
    const auto loaded = lexicon.load(path);
    if (!written.success || !loaded.success) return 2;

    const owo::engine::FullPinyinSchema schema;
    const owo::engine::CandidateGenerator generator(lexicon);
    constexpr std::string_view queries[]{"nihao", "zhongguo", "shijie", "ceshi"};
    for (std::size_t index = 0; index < 100; ++index)
        static_cast<void>(generator.generate(schema.parse(queries[index % std::size(queries)]), 10));

    std::vector<double> samples;
    samples.reserve(1000);
    std::size_t total_candidates = 0;
    for (std::size_t index = 0; index < 1000; ++index) {
        const auto start = std::chrono::steady_clock::now();
        const auto candidates = generator.generate(schema.parse(queries[index % std::size(queries)]), 10);
        const auto elapsed = std::chrono::steady_clock::now() - start;
        samples.push_back(std::chrono::duration<double, std::micro>(elapsed).count());
        total_candidates += candidates.size();
    }
    std::sort(samples.begin(), samples.end());
    std::error_code ignored;
    if (!external_lexicon) std::filesystem::remove(path, ignored);

    unsigned processors = 0;
#ifdef _WIN32
    SYSTEM_INFO system{};
    GetSystemInfo(&system);
    processors = system.dwNumberOfProcessors;
#endif
    std::cout << "{\"benchmark\":\"owo.engine.candidate_generation\""
              << ",\"configuration\":\""
#ifdef NDEBUG
              << "Release"
#else
              << "Debug"
#endif
              << "\",\"samples\":1000,\"warmup\":100,\"lexicon_entries\":" << lexicon.size()
              << ",\"source\":\"" << (external_lexicon ? "external" : "synthetic") << "\""
              << ",\"logical_processors\":" << processors
              << ",\"total_candidates\":" << total_candidates
              << ",\"latency_us\":{\"p50\":" << percentile(samples, 0.50)
              << ",\"p95\":" << percentile(samples, 0.95)
              << ",\"p99\":" << percentile(samples, 0.99)
              << ",\"max\":" << samples.back() << "}}\n";
    return total_candidates != 0 ? 0 : 3;
}
