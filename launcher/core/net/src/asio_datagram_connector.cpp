#include <array>
#include <memory>
#include <mutex>
#include <utility>
#include <vector>

#include <boost/asio/buffer.hpp>
#include <boost/asio/error.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/ip/udp.hpp>
#include <boost/asio/post.hpp>

#include "asio_endpoint.hpp"
#include "messages.hpp"
#include "reboot/net/datagram_connector.hpp"

namespace rb::net {

namespace {

namespace asio = boost::asio;
using asio::ip::udp;

// The socket stays on the I/O thread; callbacks run under `mutex`, so none outlives the channel.
struct ChannelState : std::enable_shared_from_this<ChannelState> {
    explicit ChannelState(asio::io_context& io) : socket(io) {}

    void receive() {
        socket.async_receive(asio::buffer(buffer), [self = shared_from_this()](const boost::system::error_code& error,
                                                                               std::size_t size) {
            if (error == asio::error::operation_aborted) return;
            if (!error || error == asio::error::message_size) {
                self->deliver([&](DatagramCallbacks& handlers) {
                    if (handlers.on_datagram) handlers.on_datagram(std::span<const u8>(self->buffer.data(), size));
                });
                self->receive();
                return;
            }
            // Windows reports an ICMP port unreachable on UDP as a reset.
            const bool refused = error == asio::error::connection_refused || error == asio::error::connection_reset;
            self->deliver([&](DatagramCallbacks& handlers) {
                if (handlers.on_failure)
                    handlers.on_failure(refused ? DatagramFailure::Refused : DatagramFailure::Failed, system_error(error));
            });
            if (refused) self->receive();
        });
    }

    template <class F>
    void deliver(F&& invoke) {
        const std::scoped_lock lock(mutex);
        if (!closed) invoke(callbacks);
    }

    udp::socket socket;
    std::mutex mutex;
    bool closed = false;
    DatagramCallbacks callbacks;
    std::array<u8, 2048> buffer{};
};

class AsioDatagramChannel final : public IDatagramChannel {
public:
    explicit AsioDatagramChannel(std::shared_ptr<ChannelState> state) : state_(std::move(state)) {}
    AsioDatagramChannel(const AsioDatagramChannel&) = delete;
    AsioDatagramChannel& operator=(const AsioDatagramChannel&) = delete;

    ~AsioDatagramChannel() override {
        {
            const std::scoped_lock lock(state_->mutex);
            state_->closed = true;
            state_->callbacks = {};
        }
        asio::post(state_->socket.get_executor(), [state = state_] {
            boost::system::error_code ignored;
            state->socket.close(ignored);
        });
    }

    void send(std::span<const u8> bytes) override {
        auto payload = std::make_shared<std::vector<u8>>(bytes.begin(), bytes.end());
        asio::post(state_->socket.get_executor(), [state = state_, payload] {
            state->socket.async_send(asio::buffer(*payload), [state, payload](const boost::system::error_code& error, std::size_t) {
                if (!error || error == asio::error::operation_aborted) return;
                const bool refused = error == asio::error::connection_refused || error == asio::error::connection_reset;
                state->deliver([&](DatagramCallbacks& handlers) {
                    if (handlers.on_failure)
                        handlers.on_failure(refused ? DatagramFailure::Refused : DatagramFailure::Failed, system_error(error));
                });
            });
        });
    }

private:
    std::shared_ptr<ChannelState> state_;
};

class AsioDatagramConnector final : public IDatagramConnector {
public:
    explicit AsioDatagramConnector(asio::io_context& io) : io_(io) {}

    Result<std::unique_ptr<IDatagramChannel>> connect(Endpoint target, DatagramCallbacks callbacks) override {
        auto state = std::make_shared<ChannelState>(io_);
        const udp::endpoint remote(to_asio(target.address), target.port.value);
        boost::system::error_code error;
        state->socket.open(remote.protocol(), error);
        if (!error) state->socket.connect(remote, error);
        if (error)
            return make_diag(ErrorDomain::Net, kUdpSocketFailed)
                .arg("endpoint", target.to_string())
                .os(system_error(error))
                .fail();
        state->callbacks = std::move(callbacks);
        asio::post(io_, [state] { state->receive(); });
        return std::make_unique<AsioDatagramChannel>(std::move(state));
    }

private:
    asio::io_context& io_;
};

}  // namespace

std::unique_ptr<IDatagramConnector> make_asio_datagram_connector(boost::asio::io_context& io) {
    return std::make_unique<AsioDatagramConnector>(io);
}

}  // namespace rb::net
