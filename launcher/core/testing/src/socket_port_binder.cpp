#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <boost/asio/io_context.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/ip/udp.hpp>

#include "current_process.hpp"
#include "messages.hpp"
#include "reboot/testing/port_binder.hpp"

namespace rb::testing {
namespace {

namespace asio = boost::asio;
using asio::ip::tcp;
using asio::ip::udp;

[[nodiscard]] Endpoint to_endpoint(const asio::ip::address& address, unsigned short port) {
    return Endpoint{IpAddress::v4(address.to_v4().to_uint()), Port{port}};
}

[[nodiscard]] Diagnostic socket_failed(std::string_view operation, const boost::system::error_code& error) {
    return make_diag(kTestingDomain, msg::kSocketFailed)
        .arg("operation", operation)
        .arg("endpoint", "127.0.0.1")
        .detail(error.message())
        .os(SystemError{SystemError::Origin::Host, error.value()});
}

// Loopback only, so no firewall prompt appears on a developer machine.
class SocketPortBinder final : public IPortBinder {
public:
    Result<Endpoint> bind_tcp() override {
        boost::system::error_code error;
        tcp::acceptor acceptor(io_);
        acceptor.open(tcp::v4(), error);
        if (!error) acceptor.bind(tcp::endpoint(asio::ip::address_v4::loopback(), 0), error);
        if (!error) acceptor.listen(asio::socket_base::max_listen_connections, error);
        if (error) return std::unexpected(socket_failed("tcp bind", error));
        const tcp::endpoint local = acceptor.local_endpoint(error);
        if (error) return std::unexpected(socket_failed("tcp local_endpoint", error));
        acceptors_.push_back(std::move(acceptor));
        return to_endpoint(local.address(), local.port());
    }

    Result<Port> bind_udp() override {
        boost::system::error_code error;
        udp::socket socket(io_);
        socket.open(udp::v4(), error);
        if (!error) socket.bind(udp::endpoint(asio::ip::address_v4::loopback(), 0), error);
        if (error) return std::unexpected(socket_failed("udp bind", error));
        const udp::endpoint local = socket.local_endpoint(error);
        if (error) return std::unexpected(socket_failed("udp local_endpoint", error));
        udp_.push_back(std::move(socket));
        return Port{local.port()};
    }

    Result<std::pair<Endpoint, Endpoint>> connect_loopback() override {
        boost::system::error_code error;
        tcp::acceptor acceptor(io_);
        acceptor.open(tcp::v4(), error);
        if (!error) acceptor.bind(tcp::endpoint(asio::ip::address_v4::loopback(), 0), error);
        if (!error) acceptor.listen(1, error);
        if (error) return std::unexpected(socket_failed("tcp listen", error));
        const tcp::endpoint listening = acceptor.local_endpoint(error);
        if (error) return std::unexpected(socket_failed("tcp local_endpoint", error));
        tcp::socket client(io_);
        client.connect(listening, error);
        if (error) return std::unexpected(socket_failed("tcp connect", error));
        tcp::socket server(io_);
        acceptor.accept(server, error);
        if (error) return std::unexpected(socket_failed("tcp accept", error));
        const tcp::endpoint ours = server.local_endpoint(error);
        const tcp::endpoint theirs = error ? tcp::endpoint{} : server.remote_endpoint(error);
        if (error) return std::unexpected(socket_failed("tcp endpoints", error));
        connections_.push_back(std::move(client));
        connections_.push_back(std::move(server));
        return std::pair{to_endpoint(ours.address(), ours.port()), to_endpoint(theirs.address(), theirs.port())};
    }

    void release(Port port) override {
        boost::system::error_code ignored;
        std::erase_if(acceptors_, [&](tcp::acceptor& acceptor) {
            if (acceptor.local_endpoint(ignored).port() != port.value) return false;
            acceptor.close(ignored);
            return true;
        });
        std::erase_if(udp_, [&](udp::socket& socket) {
            if (socket.local_endpoint(ignored).port() != port.value) return false;
            socket.close(ignored);
            return true;
        });
    }

    [[nodiscard]] u32 owner_pid() const override { return current_process_id(); }

private:
    asio::io_context io_;
    std::vector<tcp::acceptor> acceptors_;
    std::vector<udp::socket> udp_;
    std::vector<tcp::socket> connections_;
};

}  // namespace

std::unique_ptr<IPortBinder> make_socket_port_binder() { return std::make_unique<SocketPortBinder>(); }

}  // namespace rb::testing
