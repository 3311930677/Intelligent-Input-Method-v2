#include "owo/plugin/package_signature.h"

#include <string>

namespace {

const std::string digest(64, 'a');

std::string valid() {
    return "{\"schema_version\":1,\"inventory_sha256\":\"" + digest +
           "\",\"format\":\"cms-detached-sha256\",\"signature_base64\":\"MAMCAQE=\"}";
}

bool rejected(std::string json, const std::string& from, const std::string& to) {
    const auto position = json.find(from);
    if (position == std::string::npos) return false;
    json.replace(position, from.size(), to);
    return !owo::plugin::parse_package_signature(json).ok;
}

}  // namespace

int main() {
    const auto parsed = owo::plugin::parse_package_signature(valid());
    if (!parsed.ok || parsed.value.schema_version != 1 || parsed.value.cms_der.size() != 5 ||
        parsed.value.inventory_sha256 != digest) return 1;
    if (owo::plugin::package_signature_content(digest) !=
        "OwOPackageInventoryV1:" + digest + "\n") return 2;
    if (!owo::plugin::package_signature_content("bad").empty()) return 3;
    if (!rejected(valid(), "\"schema_version\":1", "\"schema_version\":2")) return 4;
    if (!rejected(valid(), "cms-detached-sha256", "rsa-sha256")) return 5;
    if (!rejected(valid(), std::string(64, 'a'), std::string(63, 'a') + "g")) return 6;
    if (!rejected(valid(), "MAMCAQE=", "MAMCAQF=")) return 7;  // non-canonical pad bits
    if (!rejected(valid(), "MAMCAQE=", "AQIDBA==")) return 8;  // not a DER sequence
    if (!rejected(valid(), "\"format\"", "\"unknown\":false,\"format\"")) return 9;
    if (!rejected(valid(), "\"format\":", "\"format\":\"cms-detached-sha256\",\"format\":")) return 10;
    if (owo::plugin::parse_package_signature(valid() + "x").ok) return 11;
    return 0;
}
