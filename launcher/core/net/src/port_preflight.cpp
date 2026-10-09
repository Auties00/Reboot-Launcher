#include "reboot/net/port_preflight.hpp"

#include <string>
#include <utility>

#include <boost/asio/detail/socket_option.hpp>
#include <boost/asio/error.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/ip/udp.hpp>

#include "asio_endpoint.hpp"
#include "messages.hpp"
#include "reboot/net/port_owner_service.hpp"

namespace reboot::net {

namespace {

namespace asio = boost::asio;

#ifdef _WIN32
// Without it Windows lets a later SO_REUSEADDR socket steal the port, so the test would pass.
using ExclusiveAddressUse = asio::detail::socket_option::boolean<SOL_SOCKET, SO_EXCLUSIVEADDRUSE>;
#endif

template <class Socket, class Endpoint>
[[nodiscard]] Result<PortAvailability> try_bind(Socket& socket, const Endpoint& endpoint, PortProtocol protocol) {
    const auto failed = [&](const boost::system::error_code& error) {
        return make_diag(ErrorDomain::Net, kPortTestFailed)
            .arg("protocol", std::string(protocol_name(protocol)))
            .arg("port", endpoint.port())
            .os(system_error(error))
            .fail();
    };
    boost::system::error_code error;
    socket.open(endpoint.protocol(), error);
    if (error) return failed(error);
#ifdef _WIN32
    socket.set_option(ExclusiveAddressUse(true), error);
    if (error) return failed(error);
#endif
    socket.bind(endpoint, error);
    boost::system::error_code ignored;
    socket.close(ignored);
    if (!error) return PortAvailability::Free;
    if (error == asio::error::address_in_use) return PortAvailability::InUse;
    if (error == asio::error::access_denied) return PortAvailability::AccessDenied;
    return failed(error);
}

}  // namespace

struct PortPreflight::Impl {
    Impl(asio::io_context& io_in, PortOwnerService& owners_in) : io(io_in), owners(owners_in) {}

    asio::io_context& io;
    PortOwnerService& owners;
};

PortPreflight::PortPreflight(boost::asio::io_context& io, PortOwnerService& owners)
    : impl_(std::make_unique<Impl>(io, owners)) {}

PortPreflight::~PortPreflight() = default;

Result<PortAvailability> PortPreflight::test(PortProtocol protocol, Endpoint bind) {
    const asio::ip::address address = to_asio(bind.address);
    if (protocol == PortProtocol::Udp) {
        asio::ip::udp::socket socket(impl_->io);
        return try_bind(socket, asio::ip::udp::endpoint(address, bind.port.value), protocol);
    }
    asio::ip::tcp::acceptor acceptor(impl_->io);
    return try_bind(acceptor, asio::ip::tcp::endpoint(address, bind.port.value), protocol);
}

Result<std::expected<void, PortConflict>> PortPreflight::require_free(PortProtocol protocol, IpAddress address,
                                                                      std::span<const Port> block,
                                                                      std::span<const OurProcess> ours) {
    for (const Port port : block) {
        const Endpoint bind{address, port};
        Result<PortAvailability> availability = test(protocol, bind);
        if (!availability) return std::unexpected(std::move(availability.error()));
        if (*availability == PortAvailability::Free) continue;

        PortConflict conflict;
        conflict.kind = *availability == PortAvailability::AccessDenied ? PortConflictKind::AccessDenied : PortConflictKind::InUse;
        conflict.protocol = protocol;
        conflict.bind = bind;
        Result<std::vector<PortOwnerInfo>> owners = impl_->owners.owners(protocol, bind, ours);
        if (owners) conflict.owners = std::move(*owners);
        else conflict.lookup_error = std::move(owners.error());
        // Windows can deny an exclusive bind over another socket; a held port is in use, not reserved.
        if (!conflict.owners.empty()) conflict.kind = PortConflictKind::InUse;
        return std::expected<void, PortConflict>(std::unexpect, std::move(conflict));
    }
    return std::expected<void, PortConflict>{};
}

}  // namespace reboot::net
