#pragma once

#include <memory>

#include "reboot/foundation/diag.hpp"
#include "reboot/foundation/net_types.hpp"
#include "reboot/ports/ipc.hpp"

namespace boost::asio {
class io_context;
}

namespace reboot::testing {

// A loopback TCP connection as an IByteStream, the way a game-control peer reaches the engine.
// Callbacks run on `io`'s thread; reading starts at once, and bytes wait for on_read.
[[nodiscard]] Result<std::unique_ptr<ports::IByteStream>> connect_tcp(boost::asio::io_context& io, Endpoint endpoint);

}  // namespace reboot::testing
