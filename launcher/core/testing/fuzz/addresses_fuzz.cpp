#include <cstddef>
#include <cstdint>
#include <string_view>

#include "reboot/foundation/diag.hpp"
#include "reboot/foundation/net_types.hpp"
#include "reboot/foundation/types.hpp"
#include "reboot/foundation/version.hpp"
#include "reboot/testing/fuzz.hpp"

using rb::testing::fuzz_require;

// Every text parser that takes user or wire input: what parses must print back to itself.
extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
    const std::string_view text(reinterpret_cast<const char*>(data), size);

    (void)rb::parse_port(text);
    (void)rb::parse_host_port(text);

    if (const auto address = rb::IpAddress::parse(text)) {
        const auto again = rb::IpAddress::parse(address->to_string());
        fuzz_require(again && *again == *address, "IpAddress does not round-trip");
    }
    if (const auto version = rb::GameVersion::parse(text)) {
        const std::string canonical = version->canonical();
        fuzz_require(canonical.size() <= 16, "GameVersion canonical form exceeds 16 bytes");
        const auto again = rb::GameVersion::parse(canonical);
        fuzz_require(again && *again == *version, "GameVersion does not round-trip");
    }
    if (const auto semver = rb::SemVer::parse(text)) {
        const auto again = rb::SemVer::parse(semver->to_string());
        fuzz_require(again && *again == *semver, "SemVer does not round-trip");
    }
    if (const auto uuid = rb::parse_uuid(text)) {
        const auto again = rb::parse_uuid(rb::format_uuid(*uuid));
        fuzz_require(again && *again == *uuid, "Uuid does not round-trip");
    }
    return 0;
}
