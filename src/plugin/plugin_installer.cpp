#include "owo/plugin/plugin_installer.h"

#include "owo/plugin/package_archive.h"
#include "owo/plugin/package_extraction.h"
#include "owo/plugin/package_signature.h"
#include "owo/plugin/plugin_store.h"

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#endif

#include <atomic>
#include <utility>

namespace owo::plugin {
namespace {

PluginInstallResult failure(const PluginInstallStage stage, std::string diagnostic) {
    PluginInstallResult result;
    result.stage = stage;
    result.diagnostic = std::move(diagnostic);
    return result;
}

#ifdef _WIN32
std::filesystem::path transaction_staging_path(const std::filesystem::path& root) {
    static std::atomic<std::uint64_t> sequence{};
    const auto name = L".install-" + std::to_wstring(GetCurrentProcessId()) + L"-" +
                      std::to_wstring(GetTickCount64()) + L"-" +
                      std::to_wstring(sequence.fetch_add(1, std::memory_order_relaxed));
    return root / L"staging" / name;
}

bool remove_transaction_staging(const std::filesystem::path& root,
                                const std::filesystem::path& staging) {
    const auto normalized = staging.lexically_normal();
    const auto expected_parent = (root / L"staging").lexically_normal();
    if (normalized.parent_path() != expected_parent ||
        !normalized.filename().native().starts_with(L".install-")) return false;
    std::error_code error;
    std::filesystem::remove_all(normalized, error);
    return !error && !std::filesystem::exists(normalized, error) && !error;
}
#endif

}  // namespace

PluginInstallResult install_plugin_package(
    const std::filesystem::path& package_path, const std::filesystem::path& plugin_store_root) {
    const auto package = inspect_package(package_path);
    if (!package.ok) return failure(PluginInstallStage::package_inspection, package.diagnostic);

    const auto trust = verify_signed_package_trust(package);
    if (!trust.ok) return failure(PluginInstallStage::publisher_trust, trust.diagnostic);

    const auto initialized = initialize_plugin_store(plugin_store_root);
    if (!initialized.ok)
        return failure(PluginInstallStage::store_initialization, initialized.diagnostic);

#ifdef _WIN32
    const auto staging = transaction_staging_path(plugin_store_root);
    const auto extracted = extract_package_to_staging(package, staging);
    if (!extracted.ok) {
        auto result = failure(PluginInstallStage::staging_extraction, extracted.diagnostic);
        std::error_code error;
        const bool staging_exists = std::filesystem::exists(staging, error);
        if (error || (staging_exists && !remove_transaction_staging(plugin_store_root, staging))) {
            result.retained_staging_path = staging;
            result.diagnostic += "; exact staging cleanup failed";
        }
        return result;
    }

    std::size_t expected_files = 0;
    std::uint64_t expected_bytes = 0;
    for (const auto& entry : package.entries) {
        if (!entry.path.ends_with('/')) {
            ++expected_files;
            expected_bytes += entry.uncompressed_size;
        }
    }
    if (extracted.files_written != expected_files || extracted.bytes_written != expected_bytes) {
        auto result = failure(PluginInstallStage::staging_extraction,
                              "staging output counters do not match the immutable package snapshot");
        if (!remove_transaction_staging(plugin_store_root, staging)) {
            result.retained_staging_path = staging;
            result.diagnostic += "; exact staging cleanup failed";
        }
        return result;
    }

    const auto published = publish_staged_plugin(plugin_store_root, staging,
                                                 trust.inventory_sha256,
                                                 trust.certificate_sha256);
    if (!published.ok) {
        auto result = failure(PluginInstallStage::version_publication, published.diagnostic);
        result.version_published = published.version_published;
        result.manifest = published.manifest;
        result.installed_path = published.installed_path;
        result.previous_version = published.previous_version;
        result.inventory_sha256 = trust.inventory_sha256;
        result.publisher_display_name = trust.publisher_display_name;
        result.publisher_certificate_sha256 = trust.certificate_sha256;
        std::error_code error;
        const bool staging_exists = std::filesystem::exists(staging, error);
        if (!published.version_published &&
            (error || (staging_exists && !remove_transaction_staging(plugin_store_root, staging)))) {
            result.retained_staging_path = staging;
            result.diagnostic += "; exact staging cleanup failed";
        }
        return result;
    }

    PluginInstallResult result;
    result.ok = true;
    result.stage = PluginInstallStage::completed;
    result.version_published = true;
    result.activated = true;
    result.manifest = published.manifest;
    result.installed_path = published.installed_path;
    result.previous_version = published.previous_version;
    result.inventory_sha256 = trust.inventory_sha256;
    result.publisher_display_name = trust.publisher_display_name;
    result.publisher_certificate_sha256 = trust.certificate_sha256;
    return result;
#else
    static_cast<void>(plugin_store_root);
    return failure(PluginInstallStage::staging_extraction,
                   "plugin installation is currently available on Windows only");
#endif
}

}  // namespace owo::plugin
