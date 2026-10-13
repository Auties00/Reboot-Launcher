#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdlib>
#include <optional>
#include <string>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "reboot/foundation/types.hpp"
#include "reboot/net/port_mapper_service.hpp"
#include "reboot/net/port_mapping_gateway.hpp"

// The adapters against a real miniupnpd (ci/miniupnpd): the CI Linux job runs this binary with
// "[integration]" in a container on the daemon's bridge, with REBOOT_MINIUPNPD set to its address
// and the default route through it. Everywhere else these cases skip.

using namespace rb;
using namespace rb::net;
using namespace std::chrono_literals;

namespace {

constexpr std::chrono::milliseconds kTimeout = 5s;

[[nodiscard]] std::optional<IpAddress> daemon_address() {
#ifdef _WIN32
    char* value = nullptr;
    std::size_t size = 0;
    if (_dupenv_s(&value, &size, "REBOOT_MINIUPNPD") != 0 || value == nullptr) return std::nullopt;
    const std::string text(value);
    std::free(value);
#else
    const char* value = std::getenv("REBOOT_MINIUPNPD");
    if (value == nullptr) return std::nullopt;
    const std::string text(value);
#endif
    if (text.empty()) return std::nullopt;
    return IpAddress::parse(text);
}

[[nodiscard]] SessionId session(u8 seed) {
    SessionId id;
    id.value.bytes.fill(seed);
    return id;
}

[[nodiscard]] std::optional<GatewayEntry> listed(IPortMappingGateway& gateway, Port external) {
    auto entries = gateway.list(kTimeout);
    REQUIRE(entries);
    const auto found = std::ranges::find(*entries, external, &GatewayEntry::external);
    if (found == entries->end()) return std::nullopt;
    return *found;
}

}  // namespace

TEST_CASE("miniupnpc discovers, maps, lists and removes against miniupnpd", "[net][gateway][integration]") {
    const std::optional<IpAddress> daemon = daemon_address();
    if (!daemon) SKIP("REBOOT_MINIUPNPD names no miniupnpd to test against");
    const auto upnp = make_miniupnpc_gateway();
    const auto info = upnp->discover(kTimeout, {});
    REQUIRE(info);
    REQUIRE(info->external_address);
    CHECK(info->lan_address != *daemon);

    // Our full-length description survives miniupnpc's 80-byte buffer.
    const std::string description = mapping_description("00112233aabbccdd", session(0x5a));
    REQUIRE(description.size() == 41);
    const GatewayMappingRequest ours{Port{41000}, Port{41000}, info->lan_address, 3600s, description};
    const auto mapped = upnp->add(ours, kTimeout);
    REQUIRE(mapped);
    CHECK(mapped->external == Port{41000});
    const auto entry = listed(*upnp, Port{41000});
    REQUIRE(entry);
    CHECK(entry->description == description);
    CHECK(entry->lan_address == info->lan_address);
    CHECK(entry->internal == Port{41000});

    SECTION("an external port another client holds is 718") {
        // secure_mode=no lets us map for another LAN address, as a second machine would.
        IpAddress other = info->lan_address;
        other.bytes[15] ^= 0x40;
        const GatewayMappingRequest theirs{Port{41001}, Port{41001}, other, 3600s, "someone else"};
        REQUIRE(upnp->add(theirs, kTimeout));
        const auto taken = upnp->add(GatewayMappingRequest{Port{41001}, Port{41001}, info->lan_address, 3600s, description},
                                     kTimeout);
        REQUIRE_FALSE(taken);
        CHECK(taken.error().code == GatewayErrorCode::ExternalPortTaken);
        CHECK(taken.error().protocol_code == 718);
        REQUIRE(upnp->remove(PortMapping{Port{41001}, Port{41001}, MappingMethod::Upnp, 3600s, other}, kTimeout));
    }

    // UPnP deletes by external port; a second delete finds nothing (714), which counts as done.
    REQUIRE(upnp->remove(*mapped, kTimeout));
    CHECK_FALSE(listed(*upnp, Port{41000}));
    CHECK(upnp->remove(*mapped, kTimeout));
}

TEST_CASE("libnatpmp discovers, maps and removes through the default gateway", "[net][gateway][integration]") {
    if (!daemon_address()) SKIP("REBOOT_MINIUPNPD names no miniupnpd to test against");
    const auto natpmp = make_natpmp_gateway();
    const auto info = natpmp->discover(kTimeout, {});
    REQUIRE(info);
    REQUIRE(info->external_address);

    const auto mapped = natpmp->add(GatewayMappingRequest{Port{42000}, Port{42000}, info->lan_address, 120s, {}}, kTimeout);
    REQUIRE(mapped);
    CHECK(mapped->method == MappingMethod::NatPmp);
    CHECK(mapped->internal == Port{42000});
    CHECK(mapped->external.value != 0);
    CHECK(mapped->lease > 0s);
    CHECK(mapped->lease <= 120s);
    CHECK(natpmp->list(kTimeout).error().code == GatewayErrorCode::Unsupported);

    // NAT-PMP deletes by internal port; deleting it again is no error either.
    REQUIRE(natpmp->remove(*mapped, kTimeout));
    CHECK(natpmp->remove(*mapped, kTimeout));
}
