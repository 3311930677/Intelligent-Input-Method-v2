#include "owo/engine/user_frequency.h"

#include <filesystem>
#include <fstream>
#include <iostream>

int main() {
    auto path = std::filesystem::temp_directory_path() / "owo-user-frequency-test.bin";
    std::error_code ignored;
    std::filesystem::remove(path, ignored);
    std::filesystem::remove(path.wstring() + L".bak", ignored);

    owo::engine::UserFrequencyStore store;
    if (!store.load(path).success) return 1;
    store.record("你好", 2);
    if (!store.flush().success) return 1;
    store.record("你好", 3);
    store.record("西安");
    if (!store.flush().success) return 1;

    owo::engine::UserFrequencyStore loaded;
    if (!loaded.load(path).success || loaded.count("你好") != 5 ||
        loaded.count("西安") != 1 || loaded.score("你好") <= loaded.score("西安"))
        return 1;

    { std::ofstream corrupt(path, std::ios::binary | std::ios::trunc); corrupt << "broken"; }
    owo::engine::UserFrequencyStore recovered;
    const auto recovery = recovered.load(path);
    if (!recovery.success || !recovery.recovered_from_backup || recovered.count("你好") != 2) {
        std::cerr << "backup recovery failed\n";
        return 1;
    }
    std::filesystem::remove(path, ignored);
    std::filesystem::remove(path.wstring() + L".bak", ignored);
    return 0;
}
