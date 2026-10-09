#include "reboot/game_channel/game_channel_listener.hpp"

#include <memory>
#include <optional>
#include <string>
#include <utility>

#include <boost/asio/bind_executor.hpp>
#include <boost/asio/detail/socket_option.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/post.hpp>
#include <boost/asio/steady_timer.hpp>
#include <boost/asio/strand.hpp>

#include "channel_core.hpp"
#include "peer_impls.hpp"
#include "reboot/foundation/log.hpp"
#include "reboot/game_channel/token_registry.hpp"
#include "reboot/ports/ipc.hpp"
#include "tcp_byte_stream.hpp"

namespace reboot::game_channel {

namespace {

namespace asio = boost::asio;
using asio::ip::tcp;

#ifdef _WIN32
// Without it Windows lets a later SO_REUSEADDR socket take over the port.
using ExclusiveAddressUse = asio::detail::socket_option::boolean<SOL_SOCKET, SO_EXCLUSIVEADDRUSE>;
#endif

constexpr std::chrono::milliseconds kAcceptRetry{50};

// The listening socket; every operation on it runs on its own Asio strand.
struct Acceptor : std::enable_shared_from_this<Acceptor> {
    Acceptor(asio::io_context& io_ref, Executor& strand_ref, std::weak_ptr<ChannelCore> owner)
        : io(io_ref),
          executor(asio::make_strand(io_ref)),
          acceptor(executor),
          retry(executor),
          strand(strand_ref),
          core(std::move(owner)) {}

    void accept_next() {
        acceptor.async_accept(
            asio::make_strand(io),
            asio::bind_executor(executor, [self = shared_from_this()](const boost::system::error_code& error, tcp::socket socket) {
                if (error == asio::error::operation_aborted || !self->acceptor.is_open()) return;
                if (error) {
                    // Out of descriptors, say: back off instead of spinning on the same failure.
                    REBOOT_LOG_WARN(Play, "game channel accept failed: {}", error.message());
                    self->retry.expires_after(kAcceptRetry);
                    self->retry.async_wait(asio::bind_executor(
                        self->executor, [self](const boost::system::error_code&) { self->accept_next(); }));
                    return;
                }
                self->strand.post([core = self->core, stream = make_tcp_byte_stream(std::move(socket))]() mutable {
                    if (const auto owner = core.lock()) owner->adopt(std::move(stream));
                });
                self->accept_next();
            }));
    }

    asio::io_context& io;
    asio::strand<asio::io_context::executor_type> executor;
    tcp::acceptor acceptor;
    asio::steady_timer retry;
    Executor& strand;
    std::weak_ptr<ChannelCore> core;
};

[[nodiscard]] Diagnostic listen_failed(const boost::system::error_code& error) {
    return to_diagnostic(GameChannelError{.code = GameChannelErrorCode::ListenFailed,
                                          .os_error = SystemError{SystemError::Origin::Host, error.value()}});
}

}  // namespace

struct GameChannelListener::Impl {
    Impl(asio::io_context& io_ref, Executor& strand, TimerService& timers, TokenRegistry& tokens)
        : io(io_ref), core(std::make_shared<ChannelCore>(strand, timers, tokens)) {}

    asio::io_context& io;
    std::shared_ptr<ChannelCore> core;
    std::shared_ptr<Acceptor> acceptor;
    std::optional<Port> port;
};

GameChannelListener::GameChannelListener(asio::io_context& io, Executor& strand, TimerService& timers, TokenRegistry& tokens)
    : impl_(std::make_unique<Impl>(io, strand, timers, tokens)) {}

GameChannelListener::~GameChannelListener() { close(); }

Result<Port> GameChannelListener::start() {
    if (impl_->port) return *impl_->port;
    auto acceptor = std::make_shared<Acceptor>(impl_->io, impl_->core->strand(), impl_->core);
    // 127.0.0.1 rather than localhost: Wine maps the IPv4 loopback, and nothing else may reach it.
    const tcp::endpoint loopback(asio::ip::address_v4::loopback(), 0);
    boost::system::error_code error;
    acceptor->acceptor.open(loopback.protocol(), error);
#ifdef _WIN32
    if (!error) acceptor->acceptor.set_option(ExclusiveAddressUse(true), error);
#endif
    if (!error) acceptor->acceptor.bind(loopback, error);
    if (!error) acceptor->acceptor.listen(asio::socket_base::max_listen_connections, error);
    tcp::endpoint bound;
    if (!error) bound = acceptor->acceptor.local_endpoint(error);
    if (error) {
        boost::system::error_code ignored;
        acceptor->acceptor.close(ignored);
        REBOOT_LOG_ERROR(Play, "game channel cannot listen: {}", error.message());
        return std::unexpected(listen_failed(error));
    }
    impl_->core->reopen();
    impl_->port = Port{bound.port()};
    impl_->acceptor = acceptor;
    asio::post(acceptor->executor, [acceptor] { acceptor->accept_next(); });
    REBOOT_LOG_INFO(Play, "game channel listening on 127.0.0.1:{}", bound.port());
    return *impl_->port;
}

void GameChannelListener::close() {
    if (const std::shared_ptr<Acceptor> acceptor = std::exchange(impl_->acceptor, {})) {
        asio::post(acceptor->executor, [acceptor] {
            boost::system::error_code ignored;
            acceptor->acceptor.close(ignored);
            acceptor->retry.cancel();
        });
    }
    impl_->port.reset();
    impl_->core->close();
}

Result<std::string> GameChannelListener::ctl_url() const {
    if (!impl_->port) return std::unexpected(to_diagnostic(GameChannelError{.code = GameChannelErrorCode::NotListening}));
    return "tcp://127.0.0.1:" + std::to_string(impl_->port->value);
}

Result<std::unique_ptr<ClientDllPeer>> GameChannelListener::open_client_dll(SessionId session, std::string module,
                                                                            RunnerMultiplier multiplier,
                                                                            ClientDllHandlers handlers) {
    PeerKey key{session, contracts::game_client::PeerRole::ClientDll, std::move(module)};
    auto token = impl_->core->tokens().issue(key, multiplier);
    if (!token) return std::unexpected(std::move(token.error()));
    auto impl = std::make_unique<ClientDllPeer::Impl>(*impl_->core, std::move(key), std::move(*token), std::move(handlers));
    return std::unique_ptr<ClientDllPeer>(new ClientDllPeer(std::move(impl)));
}

Result<std::unique_ptr<WinhostPeer>> GameChannelListener::open_winhost(SessionId session, RunnerMultiplier multiplier,
                                                                       WinhostHandlers handlers) {
    PeerKey key{session, contracts::game_client::PeerRole::Winhost, std::string(kWinhostModule)};
    auto token = impl_->core->tokens().issue(key, multiplier);
    if (!token) return std::unexpected(std::move(token.error()));
    auto impl = std::make_unique<WinhostPeer::Impl>(*impl_->core, std::move(key), std::move(*token), std::move(handlers));
    return std::unique_ptr<WinhostPeer>(new WinhostPeer(std::move(impl)));
}

void GameChannelListener::adopt(std::unique_ptr<ports::IByteStream> stream) { impl_->core->adopt(std::move(stream)); }

}  // namespace reboot::game_channel
