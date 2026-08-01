#include "owo/config/config_store.h"

#include <filesystem>
#include <fstream>
#include <string>

namespace {

void write(const std::filesystem::path& path, const std::string_view text) {
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    output.write(text.data(), static_cast<std::streamsize>(text.size()));
}

}  // namespace

int main(const int argc, char** argv) {
    if (argc != 2) return 1;
    const std::filesystem::path root(argv[1]);
    std::error_code error;
    std::filesystem::remove_all(root, error);
    std::filesystem::create_directories(root, error);
    if (error) return 2;
    const auto path = root / "owo.conf";

    owo::config::ConfigStore store;
    const auto defaults = store.load(path);
    if (!defaults.success || !defaults.used_defaults || defaults.generation != 1 ||
        store.snapshot() != owo::config::AppConfig{}) return 3;

    auto changed = store.snapshot();
    changed.candidate_page_size = 7;
    changed.user_learning_enabled = false;
    changed.model_ranking_enabled = true;
    changed.model_timeout_ms = 80;
    const auto first_save = store.save(changed);
    if (!first_save.success || !first_save.changed || first_save.generation != 2) return 4;
    changed.candidate_page_size = 8;
    if (!store.save(changed).success) return 5;
    if (std::filesystem::exists(path.wstring() + L".tmp") ||
        std::filesystem::exists(path.wstring() + L".bak.tmp")) return 6;

    owo::config::ConfigStore loaded;
    if (!loaded.load(path).success || loaded.snapshot() != changed) return 7;
    auto hot = changed;
    hot.model_timeout_ms = 90;
    write(path, owo::config::serialize_config(hot));
    const auto hot_reload = loaded.reload();
    if (!hot_reload.success || !hot_reload.changed || hot_reload.generation != 2 ||
        loaded.snapshot() != hot) return 8;
    const auto unchanged_reload = loaded.reload();
    if (!unchanged_reload.success || unchanged_reload.changed || unchanged_reload.generation != 2)
        return 9;
    write(path, "broken");
    const auto invalid_reload = loaded.reload();
    if (invalid_reload.success || invalid_reload.changed || loaded.snapshot() != hot ||
        loaded.generation() != 2) return 10;

    owo::config::ConfigStore recovered;
    const auto recovery = recovered.load(path);
    auto backup_value = changed;
    backup_value.candidate_page_size = 7;
    if (!recovery.success || !recovery.recovered_from_backup ||
        recovered.snapshot() != backup_value) return 11;

    write(path.wstring() + L".bak", "also broken");
    owo::config::ConfigStore fallback;
    const auto fallback_result = fallback.load(path);
    if (!fallback_result.success || !fallback_result.used_defaults ||
        fallback.snapshot() != owo::config::AppConfig{}) return 12;

    auto invalid_value = fallback.snapshot();
    invalid_value.candidate_page_size = 0;
    if (fallback.save(invalid_value).success || fallback.snapshot() != owo::config::AppConfig{} ||
        fallback.generation() != 1) return 13;

    if (owo::config::parse_config("schema_version=2\ncandidate_page_size=5\n"
            "user_learning_enabled=true\nmodel_ranking_enabled=false\nmodel_timeout_ms=50\n").ok) return 14;
    if (owo::config::parse_config("schema_version=1\ncandidate_page_size=10\n"
            "user_learning_enabled=true\nmodel_ranking_enabled=false\nmodel_timeout_ms=50\n").ok) return 15;
    if (owo::config::parse_config("schema_version=1\ncandidate_page_size=5\n"
            "user_learning_enabled=yes\nmodel_ranking_enabled=false\nmodel_timeout_ms=50\n").ok) return 16;
    if (owo::config::parse_config("schema_version=1\ncandidate_page_size=5\n"
            "user_learning_enabled=true\nmodel_ranking_enabled=false\nmodel_timeout_ms=50\nunknown=x\n").ok) return 17;
    if (owo::config::parse_config("\xef\xbb\xbfschema_version=1\n").ok) return 18;

    const auto stable = owo::config::serialize_config(changed);
    const auto round_trip = owo::config::parse_config(stable);
    if (!round_trip.ok || round_trip.value != changed ||
        stable != owo::config::serialize_config(round_trip.value)) return 19;

    std::filesystem::remove_all(root, error);
    return 0;
}
