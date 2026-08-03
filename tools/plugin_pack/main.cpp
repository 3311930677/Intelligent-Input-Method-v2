// owo_plugin_pack: developer-only .owopkg builder.
//
// This tool reproduces, byte for byte, the canonical inventory digest that
// owo::plugin::inspect_package computes, so that a signature generated over the
// reported inventory hash is accepted by the production installer without any
// change to the security-critical verification code.
//
// It only produces Store (method 0) entries, so it never depends on the audited
// zlib Deflate path. It is intended exclusively for local development packaging
// of trusted first-party plugins together with a developer test certificate.
//
// Two modes:
//   --inventory <source_dir>
//       Enumerates the source directory exactly like the final package (minus
//       signature.json), prints the lowercase inventory SHA-256 to stdout. Use
//       this hash to produce the detached CMS signature.
//   --pack <source_dir> <output.owopkg> [--signature <signature.json>]
//       Builds the final .owopkg. When --signature is given, the file is stored
//       verbatim at the package root as signature.json.

#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>
#include <bcrypt.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <string>
#include <string_view>
#include <vector>

namespace {

constexpr std::uint32_t kLocalSignature = 0x04034b50U;
constexpr std::uint32_t kCentralSignature = 0x02014b50U;
constexpr std::uint32_t kEocdSignature = 0x06054b50U;

struct FileItem {
    std::string package_path;               // Forward-slash relative path used in the archive.
    std::filesystem::path source_path;      // Absolute source path.
    std::vector<unsigned char> contents;
    std::uint32_t crc32{};
    bool is_signature{};
};

std::string to_hex(const std::vector<unsigned char>& digest) {
    static constexpr char hex[] = "0123456789abcdef";
    std::string output;
    output.reserve(digest.size() * 2U);
    for (const auto byte : digest) {
        output.push_back(hex[byte >> 4U]);
        output.push_back(hex[byte & 0x0fU]);
    }
    return output;
}

std::vector<unsigned char> sha256(const unsigned char* data, const std::size_t size) {
    BCRYPT_ALG_HANDLE algorithm = nullptr;
    BCRYPT_HASH_HANDLE hash = nullptr;
    DWORD digest_size = 0;
    DWORD result_size = 0;
    std::vector<unsigned char> digest;
    if (size > (std::numeric_limits<ULONG>::max)() ||
        BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0) < 0 ||
        BCryptGetProperty(algorithm, BCRYPT_HASH_LENGTH, reinterpret_cast<PUCHAR>(&digest_size),
                          sizeof(digest_size), &result_size, 0) < 0 ||
        BCryptCreateHash(algorithm, &hash, nullptr, 0, nullptr, 0, 0) < 0) {
        if (hash != nullptr) BCryptDestroyHash(hash);
        if (algorithm != nullptr) BCryptCloseAlgorithmProvider(algorithm, 0);
        return {};
    }
    bool valid = BCryptHashData(hash, const_cast<PUCHAR>(data), static_cast<ULONG>(size), 0) >= 0;
    digest.resize(digest_size);
    if (!valid || BCryptFinishHash(hash, digest.data(), digest_size, 0) < 0) digest.clear();
    BCryptDestroyHash(hash);
    BCryptCloseAlgorithmProvider(algorithm, 0);
    return digest;
}

std::string sha256_hex(const std::vector<unsigned char>& data) {
    const auto digest = sha256(data.data(), data.size());
    return digest.empty() ? std::string{} : to_hex(digest);
}

std::uint32_t crc32_of(const std::vector<unsigned char>& data) {
    static std::array<std::uint32_t, 256> table{};
    static bool initialized = false;
    if (!initialized) {
        for (std::uint32_t index = 0; index < 256; ++index) {
            std::uint32_t value = index;
            for (int bit = 0; bit < 8; ++bit)
                value = (value & 1U) != 0U ? 0xedb88320U ^ (value >> 1U) : value >> 1U;
            table[index] = value;
        }
        initialized = true;
    }
    std::uint32_t crc = 0xffffffffU;
    for (const auto byte : data) crc = table[(crc ^ byte) & 0xffU] ^ (crc >> 8U);
    return crc ^ 0xffffffffU;
}

void append_u16(std::vector<unsigned char>& output, const std::uint16_t value) {
    output.push_back(static_cast<unsigned char>(value));
    output.push_back(static_cast<unsigned char>(value >> 8U));
}

void append_u32(std::vector<unsigned char>& output, const std::uint32_t value) {
    append_u16(output, static_cast<std::uint16_t>(value));
    append_u16(output, static_cast<std::uint16_t>(value >> 16U));
}

void append_u64(std::vector<unsigned char>& output, const std::uint64_t value) {
    append_u32(output, static_cast<std::uint32_t>(value));
    append_u32(output, static_cast<std::uint32_t>(value >> 32U));
}

bool has_non_ascii(const std::string_view value) {
    return std::any_of(value.begin(), value.end(),
                       [](const unsigned char byte) { return byte >= 0x80U; });
}

std::string narrow_utf8(const std::wstring_view value) {
    if (value.empty()) return {};
    const auto size = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value.data(),
                                          static_cast<int>(value.size()), nullptr, 0, nullptr,
                                          nullptr);
    if (size <= 0) return {};
    std::string result(static_cast<std::size_t>(size), '\0');
    if (WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value.data(),
                            static_cast<int>(value.size()), result.data(), size, nullptr,
                            nullptr) != size)
        return {};
    return result;
}

bool read_file(const std::filesystem::path& path, std::vector<unsigned char>& output) {
    std::ifstream input(path, std::ios::binary);
    if (!input) return false;
    input.seekg(0, std::ios::end);
    const auto size = input.tellg();
    if (size < 0) return false;
    input.seekg(0, std::ios::beg);
    output.resize(static_cast<std::size_t>(size));
    if (!output.empty())
        input.read(reinterpret_cast<char*>(output.data()), static_cast<std::streamsize>(output.size()));
    return static_cast<bool>(input) || input.eof();
}

// Enumerate the source directory into archive items with forward-slash relative paths.
bool collect_items(const std::filesystem::path& source_root, std::vector<FileItem>& items,
                   std::string& error) {
    std::error_code ec;
    if (!std::filesystem::is_directory(source_root, ec)) {
        error = "source is not a directory";
        return false;
    }
    for (std::filesystem::recursive_directory_iterator it(source_root, ec), end; it != end;
         it.increment(ec)) {
        if (ec) { error = "directory iteration failed"; return false; }
        if (!it->is_regular_file(ec)) continue;
        const auto relative = std::filesystem::relative(it->path(), source_root, ec);
        if (ec) { error = "cannot compute relative path"; return false; }
        std::string package_path;
        for (const auto& part : relative) {
            if (!package_path.empty()) package_path.push_back('/');
            package_path += narrow_utf8(part.wstring());
        }
        if (package_path.empty()) { error = "empty package path"; return false; }
        FileItem item;
        item.package_path = package_path;
        item.source_path = it->path();
        item.is_signature = package_path == "signature.json";
        if (!read_file(it->path(), item.contents)) {
            error = "cannot read file: " + package_path;
            return false;
        }
        item.crc32 = crc32_of(item.contents);
        items.push_back(std::move(item));
    }
    if (items.empty()) { error = "no files found under source"; return false; }
    const bool has_manifest = std::any_of(items.begin(), items.end(),
        [](const FileItem& item) { return item.package_path == "manifest.json"; });
    if (!has_manifest) { error = "root manifest.json is required"; return false; }
    // Central directory is emitted in enumeration order; keep a stable deterministic order.
    std::sort(items.begin(), items.end(),
              [](const FileItem& left, const FileItem& right) {
                  return left.package_path < right.package_path;
              });
    return true;
}

// Reproduces owo::plugin::inspect_package's canonical inventory digest exactly.
std::string compute_inventory_sha256(const std::vector<FileItem>& items, std::string& error) {
    std::vector<const FileItem*> canonical;
    for (const auto& item : items)
        if (!item.is_signature) canonical.push_back(&item);
    std::sort(canonical.begin(), canonical.end(),
              [](const FileItem* left, const FileItem* right) {
                  return left->package_path < right->package_path;
              });
    std::vector<unsigned char> blob;
    constexpr std::array<unsigned char, 22> domain{'O', 'w', 'O', 'P', 'a', 'c', 'k', 'a', 'g',
                                                    'e', 'I', 'n', 'v', 'e', 'n', 't', 'o', 'r',
                                                    'y', 'V', '1', 0};
    blob.insert(blob.end(), domain.begin(), domain.end());
    append_u32(blob, static_cast<std::uint32_t>(canonical.size()));
    for (const auto* item : canonical) {
        const auto compressed_sha256 = sha256_hex(item->contents);  // Store method: data == file bytes.
        if (compressed_sha256.empty()) { error = "SHA-256 unavailable"; return {}; }
        append_u32(blob, static_cast<std::uint32_t>(item->package_path.size()));
        blob.insert(blob.end(), item->package_path.begin(), item->package_path.end());
        append_u16(blob, 0);  // compression method: Store.
        append_u32(blob, item->crc32);
        append_u64(blob, item->contents.size());  // compressed size == uncompressed for Store.
        append_u64(blob, item->contents.size());
        blob.insert(blob.end(), compressed_sha256.begin(), compressed_sha256.end());
    }
    const auto digest = sha256_hex(blob);
    if (digest.empty()) error = "cannot hash canonical inventory";
    return digest;
}

// Build a bounded, Store-only ZIP archive accepted by inspect_package.
bool build_archive(const std::vector<FileItem>& items, std::vector<unsigned char>& output,
                   std::string& error) {
    std::vector<unsigned char> local;
    std::vector<unsigned char> central;
    std::uint16_t entry_count = 0;
    for (const auto& item : items) {
        if (item.package_path.size() > 512) { error = "path too long: " + item.package_path; return false; }
        const auto flags = static_cast<std::uint16_t>(has_non_ascii(item.package_path) ? 0x0800U : 0U);
        const auto local_offset = static_cast<std::uint32_t>(local.size());
        const auto size = static_cast<std::uint32_t>(item.contents.size());
        // Local file header.
        append_u32(local, kLocalSignature);
        append_u16(local, 20);          // version needed.
        append_u16(local, flags);
        append_u16(local, 0);           // method: Store.
        append_u16(local, 0);           // mod time.
        append_u16(local, 0);           // mod date.
        append_u32(local, item.crc32);
        append_u32(local, size);        // compressed size.
        append_u32(local, size);        // uncompressed size.
        append_u16(local, static_cast<std::uint16_t>(item.package_path.size()));
        append_u16(local, 0);           // extra length.
        local.insert(local.end(), item.package_path.begin(), item.package_path.end());
        local.insert(local.end(), item.contents.begin(), item.contents.end());
        // Central directory header.
        append_u32(central, kCentralSignature);
        append_u16(central, 20);        // version made by.
        append_u16(central, 20);        // version needed.
        append_u16(central, flags);
        append_u16(central, 0);         // method: Store.
        append_u16(central, 0);         // mod time.
        append_u16(central, 0);         // mod date.
        append_u32(central, item.crc32);
        append_u32(central, size);
        append_u32(central, size);
        append_u16(central, static_cast<std::uint16_t>(item.package_path.size()));
        append_u16(central, 0);         // extra length.
        append_u16(central, 0);         // comment length.
        append_u16(central, 0);         // disk number.
        append_u16(central, 0);         // internal attributes.
        append_u32(central, 0);         // external attributes.
        append_u32(central, local_offset);
        central.insert(central.end(), item.package_path.begin(), item.package_path.end());
        ++entry_count;
    }
    output = std::move(local);
    const auto central_offset = static_cast<std::uint32_t>(output.size());
    const auto central_size = static_cast<std::uint32_t>(central.size());
    output.insert(output.end(), central.begin(), central.end());
    // End of central directory record.
    append_u32(output, kEocdSignature);
    append_u16(output, 0);              // this disk.
    append_u16(output, 0);              // central directory disk.
    append_u16(output, entry_count);    // entries on this disk.
    append_u16(output, entry_count);    // total entries.
    append_u32(output, central_size);
    append_u32(output, central_offset);
    append_u16(output, 0);              // comment length.
    return true;
}

bool write_file(const std::filesystem::path& path, const std::vector<unsigned char>& data) {
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    if (!output) return false;
    if (!data.empty())
        output.write(reinterpret_cast<const char*>(data.data()),
                     static_cast<std::streamsize>(data.size()));
    return static_cast<bool>(output);
}

int run_inventory(const std::filesystem::path& source) {
    std::vector<FileItem> items;
    std::string error;
    if (!collect_items(source, items, error)) {
        std::cerr << "error: " << error << '\n';
        return 2;
    }
    const auto inventory = compute_inventory_sha256(items, error);
    if (inventory.empty()) {
        std::cerr << "error: " << error << '\n';
        return 3;
    }
    std::cout << inventory << '\n';
    return 0;
}

int run_pack(const std::filesystem::path& source, const std::filesystem::path& output,
             const std::filesystem::path& signature_path, const bool has_signature) {
    std::vector<FileItem> items;
    std::string error;
    if (has_signature) {
        // Inject signature.json into the source tree as a virtual root entry.
        FileItem signature;
        signature.package_path = "signature.json";
        signature.is_signature = true;
        if (!read_file(signature_path, signature.contents)) {
            std::cerr << "error: cannot read signature file\n";
            return 4;
        }
        if (signature.contents.empty() || signature.contents.size() > 32U * 1024U) {
            std::cerr << "error: signature.json must be a non-empty stored entry <= 32768 bytes\n";
            return 4;
        }
        signature.crc32 = crc32_of(signature.contents);
        if (!collect_items(source, items, error)) {
            std::cerr << "error: " << error << '\n';
            return 2;
        }
        const bool duplicate = std::any_of(items.begin(), items.end(),
            [](const FileItem& item) { return item.package_path == "signature.json"; });
        if (duplicate) {
            std::cerr << "error: source already contains signature.json\n";
            return 4;
        }
        items.push_back(std::move(signature));
        std::sort(items.begin(), items.end(),
                  [](const FileItem& left, const FileItem& right) {
                      return left.package_path < right.package_path;
                  });
    } else if (!collect_items(source, items, error)) {
        std::cerr << "error: " << error << '\n';
        return 2;
    }
    std::vector<unsigned char> archive;
    if (!build_archive(items, archive, error)) {
        std::cerr << "error: " << error << '\n';
        return 5;
    }
    if (!write_file(output, archive)) {
        std::cerr << "error: cannot write output package\n";
        return 6;
    }
    const auto inventory = compute_inventory_sha256(items, error);
    std::cout << "packed " << items.size() << " entries, inventory_sha256=" << inventory << '\n';
    return 0;
}

}  // namespace

int wmain(const int argc, wchar_t** argv) {
    if (argc >= 3 && std::wstring_view(argv[1]) == L"--inventory") {
        return run_inventory(std::filesystem::path(argv[2]));
    }
    if (argc >= 4 && std::wstring_view(argv[1]) == L"--pack") {
        std::filesystem::path signature;
        bool has_signature = false;
        for (int index = 4; index + 1 < argc; index += 2) {
            if (std::wstring_view(argv[index]) == L"--signature") {
                signature = argv[index + 1];
                has_signature = true;
            }
        }
        return run_pack(std::filesystem::path(argv[2]), std::filesystem::path(argv[3]), signature,
                        has_signature);
    }
    std::wcerr << L"usage:\n"
               << L"  owo_plugin_pack --inventory <source_dir>\n"
               << L"  owo_plugin_pack --pack <source_dir> <output.owopkg> [--signature <signature.json>]\n";
    return 1;
}
