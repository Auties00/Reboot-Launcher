#pragma once

#include <memory>
#include <string>

#include "core/types.hpp"
#include "wire/messages.hpp"

namespace sb::edge {

// Continent lookup for region tagging. Without a database (or libmaxminddb) every address
// maps to Region::all ("unknown").
class GeoIp {
public:
    explicit GeoIp(const std::string& mmdb_path);
    ~GeoIp();
    GeoIp(const GeoIp&) = delete;
    GeoIp& operator=(const GeoIp&) = delete;

    [[nodiscard]] wire::Region lookup(const IpAddr& addr) const noexcept;
    [[nodiscard]] bool loaded() const noexcept { return impl_ != nullptr; }

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace sb::edge
