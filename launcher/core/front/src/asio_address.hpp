#pragma once

#include <boost/asio/ip/address.hpp>
#include <boost/asio/ip/tcp.hpp>

#include "reboot/foundation/net_types.hpp"

namespace rb::front {

[[nodiscard]] boost::asio::ip::address to_asio(const IpAddress& address);
[[nodiscard]] IpAddress from_asio(const boost::asio::ip::address& address) noexcept;
[[nodiscard]] Endpoint from_asio(const boost::asio::ip::tcp::endpoint& endpoint) noexcept;

}  // namespace rb::front
