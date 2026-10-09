#include "asio_address.hpp"

#include <algorithm>

namespace reboot::front {

namespace asio = boost::asio;

asio::ip::address to_asio(const IpAddress& address) {
    if (address.is_v4())
        return asio::ip::address_v4(
            asio::ip::address_v4::bytes_type{address.bytes[12], address.bytes[13], address.bytes[14], address.bytes[15]});
    asio::ip::address_v6::bytes_type bytes{};
    std::ranges::copy(address.bytes, bytes.begin());
    return asio::ip::address_v6(bytes);
}

IpAddress from_asio(const asio::ip::address& address) noexcept {
    if (address.is_v4()) return IpAddress::v4(address.to_v4().to_uint());
    IpAddress out;
    // A v4-mapped v6 address lands in the same bytes IpAddress::v4 writes.
    std::ranges::copy(address.to_v6().to_bytes(), out.bytes.begin());
    return out;
}

Endpoint from_asio(const asio::ip::tcp::endpoint& endpoint) noexcept {
    return {from_asio(endpoint.address()), Port{endpoint.port()}};
}

}  // namespace reboot::front
