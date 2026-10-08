#include <cstddef>
#include <cstdint>
#include <string_view>

#include "reboot/foundation/diag.hpp"
#include "reboot/foundation/net_types.hpp"
#include "reboot/foundation/types.hpp"
#include "reboot/foundation/version.hpp"
#include "reboot/testing/fuzz.hpp"

using reboot::testing::fuzz_require;

// Every text parser that takes user or wire input: what parses must print back to itself.
extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
    const std::string_view text(reinterpret_cast<const char*>(data), size);

    (void)reboot::parse_port(text);
    (void)reboot::parse_host_port(text);

    if (const auto address = reboot::IpAddress::parse(text)) {
        const auto again = reboot::IpAddress::parse(address->to_string());
        fuzz_require(again && *again == *address, "IpAddress does not round-trip");
    }
    if (const auto version = reboot::GameVersion::parse(text)) {
        const std::string canonical = version->canonical();
        fuzz_require(canonical.size() <= 16, "GameVersion canonical form exceeds 16 bytes");
        const auto again = reboot::GameVersion::parse(canonical);
        fuzz_require(again && *again == *version, "GameVersion does not round-trip");
    }
    if (const auto semver = reboot::SemVer::parse(text)) {
        const auto again = reboot::SemVer::parse(semver->to_string());
        fuzz_require(again && *again == *semver, "SemVer does not round-trip");
    }
    if (const auto uuid = reboot::parse_uuid(text)) {
        const auto again = reboot::parse_uuid(reboot::format_uuid(*uuid));
        fuzz_require(again && *again == *uuid, "Uuid does not round-trip");
    }
    return 0;
}
