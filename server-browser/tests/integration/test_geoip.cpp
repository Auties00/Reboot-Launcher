// Region tagging against a real country database (SB_GEOIP_DB, e.g. DB-IP Country Lite).

#include <catch2/catch_test_macros.hpp>

#include <cstdlib>

#include "edge/geoip.hpp"

using namespace sb;

namespace {

IpAddr v4(unsigned a, unsigned b, unsigned c, unsigned d) { return IpAddr::v4((a << 24) | (b << 16) | (c << 8) | d); }

IpAddr v6(std::initializer_list<u16> groups) {
    IpAddr ip;
    std::size_t i = 0;
    for (u16 g : groups) {
        ip.bytes[i++] = static_cast<u8>(g >> 8);
        ip.bytes[i++] = static_cast<u8>(g);
    }
    return ip;
}

}  // namespace

TEST_CASE("GeoIP maps public addresses to continents", "[geoip]") {
    const char* path = std::getenv("SB_GEOIP_DB");
    if (!path || !*path) SKIP("SB_GEOIP_DB not set");
    edge::GeoIp geo(path);
    REQUIRE(geo.loaded());
    CHECK(geo.lookup(v4(8, 8, 8, 8)) == wire::Region::north_america);     // Google DNS, US
    CHECK(geo.lookup(v4(193, 0, 0, 1)) == wire::Region::europe);          // RIPE NCC, NL
    CHECK(geo.lookup(v4(200, 160, 0, 1)) == wire::Region::south_america);  // NIC.br, BR
    CHECK(geo.lookup(v4(1, 0, 0, 1)) == wire::Region::oceania);           // APNIC/Cloudflare, AU
    CHECK(geo.lookup(v6({0x2001, 0x4860, 0x4860, 0, 0, 0, 0, 0x8888})) == wire::Region::north_america);
    CHECK(geo.lookup(v4(127, 0, 0, 1)) == wire::Region::all);   // loopback: unknown
    CHECK(geo.lookup(v4(10, 1, 2, 3)) == wire::Region::all);    // private: unknown
}

TEST_CASE("GeoIP without a database is a no-op", "[geoip]") {
    edge::GeoIp geo("");
    CHECK_FALSE(geo.loaded());
    CHECK(geo.lookup(v4(8, 8, 8, 8)) == wire::Region::all);
}
