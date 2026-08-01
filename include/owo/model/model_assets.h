#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace owo::model {

inline constexpr std::uint32_t kModelManifestVersion = 1;

struct ModelManifest {
    std::uint32_t manifest_version{kModelManifestVersion};
    std::string model_id;
    std::string architecture;
    std::string task;
    std::string format;
    std::string source_revision;
    std::string model_sha256;
    std::string vocabulary_sha256;
    std::string license;
    std::string model_file;
    std::string vocabulary_file;
    std::size_t maximum_sequence_length{64};
    std::size_t maximum_candidates{8};
};

struct ValidationResult {
    bool ok{};
    std::string diagnostic;
};

/// 校验 ModelHost 内部模型资产契约；不代表接受模型许可证或授权分发。
[[nodiscard]] ValidationResult validate_manifest(const ModelManifest& manifest);

struct ModelAssetBundle {
    ModelManifest manifest;
    std::filesystem::path model_path;
    std::filesystem::path vocabulary_path;
    std::vector<std::string> vocabulary;
};

struct ModelAssetLoadResult {
    bool ok{};
    ModelAssetBundle value;
    std::string diagnostic;
};

/// 从严格 key=value 文件加载并核验模型资产；文件名只能指向 manifest 同目录。
[[nodiscard]] ModelAssetLoadResult load_model_assets(const std::filesystem::path& manifest_path);

struct TokenizedInput {
    std::vector<std::int64_t> input_ids;
    std::vector<std::int64_t> attention_mask;
    std::vector<std::string> tokens;
};

struct TokenizeResult {
    bool ok{};
    TokenizedInput value;
    std::string diagnostic;
};

class WordPieceTokenizer final {
public:
    /// 词表顺序即模型 token ID；构造时要求包含四个 BERT 特殊 token。
    explicit WordPieceTokenizer(std::vector<std::string> vocabulary, bool lowercase_ascii = false);

    [[nodiscard]] ValidationResult validation() const;
    /// 严格 UTF-8；输出自动包含 [CLS]/[SEP]，超限时失败而非截断。
    [[nodiscard]] TokenizeResult encode(std::string_view text,
                                        std::size_t maximum_sequence_length) const;

private:
    std::vector<std::string> vocabulary_;
    std::unordered_map<std::string, std::int64_t> token_ids_;
    bool lowercase_ascii_{};
    ValidationResult validation_;
};

}  // namespace owo::model
