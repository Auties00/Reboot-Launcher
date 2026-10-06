#include "edge/geoip.hpp"

#include <cstring>
#include <stdexcept>

#if __has_include(<maxminddb.h>)
#include <arpa/inet.h>
#include <maxminddb.h>
#include <netinet/in.h>
#define SB_HAVE_MAXMINDDB 1
#endif

namespace sb::edge {

#if SB_HAVE_MAXMINDDB
struct GeoIp::Impl {
    MMDB_s db{};
};

GeoIp::GeoIp(const std::string& path) {
    if (path.empty()) return;
    auto impl = std::make_unique<Impl>();
    if (MMDB_open(path.c_str(), MMDB_MODE_MMAP, &impl->db) != MMDB_SUCCESS)
        throw std::runtime_error("cannot open GeoIP database " + path);
    impl_ = std::move(impl);
}

GeoIp::~GeoIp() {
    if (impl_) MMDB_close(&impl_->db);
}

wire::Region GeoIp::lookup(const IpAddr& addr) const noexcept {
    if (!impl_) return wire::Region::all;
    sockaddr_in6 sa{};
    sa.sin6_family = AF_INET6;
    std::memcpy(&sa.sin6_addr, addr.bytes.data(), 16);
    int err = 0;
    MMDB_lookup_result_s res = MMDB_lookup_sockaddr(&impl_->db, reinterpret_cast<const sockaddr*>(&sa), &err);
    if (err != MMDB_SUCCESS || !res.found_entry) return wire::Region::all;
    MMDB_entry_data_s data{};
    if (MMDB_get_value(&res.entry, &data, "continent", "code", nullptr) != MMDB_SUCCESS || !data.has_data ||
        data.type != MMDB_DATA_TYPE_UTF8_STRING || data.data_size != 2)
        return wire::Region::all;
    const std::string_view code(data.utf8_string, 2);
    if (code == "AF") return wire::Region::africa;
    if (code == "AN") return wire::Region::antarctica;
    if (code == "AS") return wire::Region::asia;
    if (code == "EU") return wire::Region::europe;
    if (code == "NA") return wire::Region::north_america;
    if (code == "OC") return wire::Region::oceania;
    if (code == "SA") return wire::Region::south_america;
    return wire::Region::all;
}
#else
struct GeoIp::Impl {};
GeoIp::GeoIp(const std::string& path) {
    if (!path.empty()) throw std::runtime_error("built without libmaxminddb; cannot load " + path);
}
GeoIp::~GeoIp() = default;
wire::Region GeoIp::lookup(const IpAddr&) const noexcept { return wire::Region::all; }
#endif

}  // namespace sb::edge
