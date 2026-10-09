#include "reboot/front/session_front.hpp"

#include <string>
#include <utility>

#include <boost/asio/post.hpp>

#include "front_core.hpp"
#include "messages.hpp"
#include "session_front_impl.hpp"
#include "strand_routes.hpp"
#include "tls_client.hpp"

namespace rb::front {

namespace {

namespace asio = boost::asio;
using tcp = asio::ip::tcp;

// Strand-only. done() waits for the listener to close and for every connection to end or be aborted.
struct StopState {
    UniqueFunction<void()> done;
    TimerHandle grace;
    bool socket_closed = false;
    bool drained = false;
    bool finished = false;
};

void finish_when_ready(StopState& state) {
    if (state.finished || !state.socket_closed || !state.drained) return;
    state.finished = true;
    state.grace.cancel();
    UniqueFunction<void()> done = std::move(state.done);
    if (done) done();
}

}  // namespace

void close_acceptor(const std::shared_ptr<tcp::acceptor>& acceptor, UniqueFunction<void()> then) {
    asio::post(acceptor->get_executor(), [acceptor, then = std::move(then)]() mutable {
        boost::system::error_code ignored;
        acceptor->close(ignored);
        if (then) then();
    });
}

SessionFront::Impl::Impl(asio::io_context& io, Executor& strand, TimerService& timers, WorkerPool& workers,
                         net::HostTlsMemory& tls, UserRequestRegistry& requests, ports::ILoopbackPeerInspector& peers,
                         FrontOptions options)
    : core(std::make_shared<FrontCore>(io, strand, timers, workers, peers, std::move(options))),
      routes(std::make_shared<StrandRoutes>(core->table, tls, requests)) {
    core->routes = routes;
}

SessionFront::Impl::~Impl() {
    if (acceptor) close_acceptor(acceptor, nullptr);
    for (const std::shared_ptr<FrontConnection>& connection : core->table.connections()) connection->abort();
}

void SessionFront::Impl::attach_legacy(SessionId session, std::vector<Port> ports) {
    static_cast<void>(core->table.set_legacy(session, std::move(ports)));
}

void SessionFront::Impl::detach_legacy() {
    for (const std::shared_ptr<FrontConnection>& connection : core->table.set_legacy(std::nullopt, {}))
        connection->abort();
}

SessionFront::SessionFront(asio::io_context& io, Executor& strand, TimerService& timers, WorkerPool& workers,
                           net::HostTlsMemory& tls, UserRequestRegistry& requests,
                           ports::ILoopbackPeerInspector& peers, FrontOptions options)
    : impl_(std::make_unique<Impl>(io, strand, timers, workers, tls, requests, peers, std::move(options))) {}

SessionFront::~SessionFront() = default;

Result<Port> SessionFront::start() {
    Impl& impl = *impl_;
    if (impl.port) return *impl.port;
    if (!impl.core->tls) impl.core->tls = make_upstream_tls_context(impl.core->options.ca_bundle);
    boost::system::error_code error;
    std::shared_ptr<tcp::acceptor> acceptor = bind_loopback(impl.core->io, Port{0}, error);
    Port port{};
    if (acceptor) port = Port{acceptor->local_endpoint(error).port()};
    if (!acceptor || error) {
        if (acceptor) acceptor->close(error);
        return make_diag(ErrorDomain::Front, msg::kListenFailed)
            .arg("address", "127.0.0.1:0")
            .os(SystemError{SystemError::Origin::Host, error.value()})
            .fail();
    }
    impl.acceptor = acceptor;
    impl.port = port;
    impl.core->table.end_stop();
    impl.core->table.set_session_port(port);
    accept_connections(impl.core, acceptor, ListenerKind::Session, port);
    return port;
}

std::optional<Port> SessionFront::port() const { return impl_->port; }

Result<std::string> SessionFront::origin(const SessionKey& key) const {
    if (!impl_->port) return make_diag(ErrorDomain::Front, msg::kNotStarted).fail();
    return "http://127.0.0.1:" + std::to_string(impl_->port->value) + "/s/" + key.to_hex() + "/";
}

void SessionFront::set_embedded_backend(std::optional<Port> http_port) { impl_->core->table.set_embedded(http_port); }

Result<void> SessionFront::add_route(FrontRoute route) {
    if (!impl_->port) return make_diag(ErrorDomain::Front, msg::kNotStarted).fail();
    return impl_->routes->add(std::move(route));
}

void SessionFront::remove_route(SessionId session) {
    for (const std::shared_ptr<FrontConnection>& connection : impl_->routes->remove(session)) connection->abort();
}

void SessionFront::stop(std::chrono::milliseconds grace, UniqueFunction<void()> done) {
    Impl& impl = *impl_;
    auto state = std::make_shared<StopState>();
    state->done = std::move(done);
    const std::shared_ptr<FrontCore> core = impl.core;
    impl.port.reset();
    core->table.set_session_port(std::nullopt);

    // On the I/O side, once the listener is closed: idle connections close, busy ones finish their exchange.
    UniqueFunction<void()> drain = [core, state] {
        Executor& strand = core->strand;
        const auto connections = core->table.begin_stop([&strand, state] {
            strand.post([state] {
                state->drained = true;
                finish_when_ready(*state);
            });
        });
        for (const std::shared_ptr<FrontConnection>& connection : connections) connection->drain();
        strand.post([state] {
            state->socket_closed = true;
            finish_when_ready(*state);
        });
    };
    if (std::shared_ptr<tcp::acceptor> acceptor = std::exchange(impl.acceptor, nullptr))
        close_acceptor(acceptor, std::move(drain));
    else
        asio::post(core->io, std::move(drain));

    state->grace = core->timers.after(grace, [core, state] {
        for (const std::shared_ptr<FrontConnection>& connection : core->table.connections()) connection->abort();
        state->drained = true;
        finish_when_ready(*state);
    });
}

}  // namespace rb::front
