#pragma once

#include <memory>

#include <boost/asio/ip/tcp.hpp>

#include "reboot/ports/ipc.hpp"

namespace rb::game_channel {

// An accepted loopback TCP socket as an IByteStream; callbacks run on the socket's executor.
// Bytes read are wiped once delivered and bytes written once sent, since a Hello carries a token
// and a Welcome may carry the game's secrets. close() lets queued writes go first.
[[nodiscard]] std::unique_ptr<ports::IByteStream> make_tcp_byte_stream(boost::asio::ip::tcp::socket socket);

}  // namespace rb::game_channel
