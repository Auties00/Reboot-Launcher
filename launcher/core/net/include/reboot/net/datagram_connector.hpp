#pragma once

#include <memory>
#include <optional>
#include <span>

#include "reboot/foundation/diag.hpp"
#include "reboot/foundation/function.hpp"
#include "reboot/foundation/net_types.hpp"
#include "reboot/foundation/types.hpp"

namespace boost::asio {
class io_context;
}

namespace reboot::net {

// Refused is an ICMP port unreachable reported on the connected socket.
enum class DatagramFailure : u8 { Refused, Failed };

// Callbacks run on an I/O thread and must only post to the strand.
struct DatagramCallbacks {
    UniqueFunction<void(std::span<const u8>)> on_datagram;
    UniqueFunction<void(DatagramFailure, std::optional<SystemError>)> on_failure;
};

// A UDP socket connected to one endpoint, so only that endpoint's datagrams arrive. Destroying
// it closes the socket, and no callback runs afterwards.
class IDatagramChannel {
public:
    virtual ~IDatagramChannel() = default;
    // A send error arrives through on_failure.
    virtual void send(std::span<const u8> bytes) = 0;
};

// Capabilities: none; the seam UdpBeaconProber is tested through.
class IDatagramConnector {
public:
    virtual ~IDatagramConnector() = default;
    virtual Result<std::unique_ptr<IDatagramChannel>> connect(Endpoint target, DatagramCallbacks callbacks) = 0;
};

// Asio UDP sockets on `io`.
[[nodiscard]] std::unique_ptr<IDatagramConnector> make_asio_datagram_connector(boost::asio::io_context& io);

}  // namespace reboot::net
