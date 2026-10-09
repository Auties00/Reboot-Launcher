#pragma once

#include <array>
#include <cstring>

#include <boost/asio/ip/address.hpp>
#include <boost/system/error_code.hpp>

#include "reboot/foundation/diag.hpp"
#include "reboot/foundation/net_types.hpp"

namespace reboot::net {

[[nodiscard]] inline boost::asio::ip::address to_asio(const IpAddress& address) {
    if (address.is_v4()) {
        boost::asio::ip::address_v4::bytes_type bytes{};
        std::memcpy(bytes.data(), address.bytes.data() + 12, 4);
        return boost::asio::ip::address_v4(bytes);
    }
    boost::asio::ip::address_v6::bytes_type bytes{};
    std::memcpy(bytes.data(), address.bytes.data(), 16);
    return boost::asio::ip::address_v6(bytes);
}

[[nodiscard]] inline IpAddress from_asio(const boost::asio::ip::address& address) {
    IpAddress out;
    if (address.is_v4()) {
        const auto bytes = address.to_v4().to_bytes();
        out.bytes[10] = 0xFF;
        out.bytes[11] = 0xFF;
        std::memcpy(out.bytes.data() + 12, bytes.data(), 4);
        return out;
    }
    const auto bytes = address.to_v6().to_bytes();
    std::memcpy(out.bytes.data(), bytes.data(), 16);
    return out;
}

[[nodiscard]] inline SystemError system_error(const boost::system::error_code& error) {
    return SystemError{SystemError::Origin::Host, error.value()};
}

}  // namespace reboot::net
