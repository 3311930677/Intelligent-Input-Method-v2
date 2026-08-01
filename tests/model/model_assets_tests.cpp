#include "owo/model/model_assets.h"

#include <string>
#include <vector>

namespace {

owo::model::ModelManifest valid_manifest() {
    return {1,
            "uer.chinese-roberta-mini.rank.v1",
            "bert",
            "masked-lm",
            "onnx",
            std::string(40, 'a'),
            std::string(64, 'b'),
            std::string(64, 'c'),
            "license-ref-local-evaluation",
            64,
            8};
}

}  // namespace

int main() {
    if (!owo::model::validate_manifest(valid_manifest()).ok) return 1;
    auto manifest = valid_manifest();
    manifest.license = "unknown";
    if (owo::model::validate_manifest(manifest).ok) return 2;
    manifest = valid_manifest();
    manifest.source_revision = "main";
    if (owo::model::validate_manifest(manifest).ok) return 3;
    manifest = valid_manifest();
    manifest.maximum_candidates = 0;
    if (owo::model::validate_manifest(manifest).ok) return 4;

    const std::vector<std::string> vocabulary = {
        "[PAD]", "[UNK]", "[CLS]", "[SEP]", "你", "好", "hello", "##s", "!"};
    const owo::model::WordPieceTokenizer tokenizer(vocabulary, true);
    if (!tokenizer.validation().ok) return 5;
    const auto chinese = tokenizer.encode("你好", 64);
    if (!chinese.ok || chinese.value.tokens !=
                           std::vector<std::string>{"[CLS]", "你", "好", "[SEP]"} ||
        chinese.value.input_ids != std::vector<std::int64_t>{2, 4, 5, 3}) return 6;
    const auto mixed = tokenizer.encode("HELLOs!", 64);
    if (!mixed.ok || mixed.value.tokens !=
                         std::vector<std::string>{"[CLS]", "hello", "##s", "!", "[SEP]"}) return 7;
    const auto unknown = tokenizer.encode("不存在", 64);
    if (!unknown.ok || unknown.value.tokens !=
                           std::vector<std::string>{"[CLS]", "[UNK]", "[UNK]", "[UNK]", "[SEP]"})
        return 8;
    if (tokenizer.encode("你好", 3).ok) return 9;
    const std::string invalid_utf8{"\xE4\xB8", 2};
    if (tokenizer.encode(invalid_utf8, 64).ok) return 10;

    const owo::model::WordPieceTokenizer missing_special({"[UNK]", "[CLS]", "[SEP]"});
    if (missing_special.validation().ok || missing_special.encode("你", 64).ok) return 11;
    return 0;
}
