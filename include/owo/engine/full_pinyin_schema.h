#pragma once

#include "owo/engine/input_schema.h"

namespace owo::engine {

class FullPinyinSchema final : public InputSchema {
public:
    [[nodiscard]] ParseResult parse(std::string_view input,
                                    std::size_t max_paths = 32) const override;
    [[nodiscard]] ParseResult parse(std::string_view input, std::size_t max_paths,
                                    bool correction_enabled) const;
};

}  // namespace owo::engine
