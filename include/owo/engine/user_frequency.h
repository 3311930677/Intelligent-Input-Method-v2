#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <unordered_map>

namespace owo::engine {

class UserFrequencyModel {
public:
    virtual ~UserFrequencyModel() = default;
    [[nodiscard]] virtual std::int64_t score(std::string_view text) const = 0;
};

struct UserFrequencyIoResult {
    bool success{};
    bool recovered_from_backup{};
    std::string error;
};

class UserFrequencyStore final : public UserFrequencyModel {
public:
    [[nodiscard]] UserFrequencyIoResult load(const std::filesystem::path& path);
    void record(std::string_view text, std::uint32_t amount = 1);
    [[nodiscard]] std::uint32_t count(std::string_view text) const;
    [[nodiscard]] std::int64_t score(std::string_view text) const override;
    [[nodiscard]] UserFrequencyIoResult flush() const;

private:
    std::filesystem::path path_;
    std::unordered_map<std::string, std::uint32_t> counts_;
};

}  // namespace owo::engine
