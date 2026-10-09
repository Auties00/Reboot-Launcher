#pragma once

#include <memory>
#include <optional>
#include <vector>

#include <boost/asio/ip/tcp.hpp>

#include "front_core.hpp"
#include "reboot/front/session_front.hpp"
#include "strand_routes.hpp"

namespace rb::front {

// Strand-only, but for `core`, which the I/O side shares.
struct SessionFront::Impl {
    Impl(boost::asio::io_context& io, Executor& strand, TimerService& timers, WorkerPool& workers,
         net::HostTlsMemory& tls, UserRequestRegistry& requests, ports::ILoopbackPeerInspector& peers,
         FrontOptions options);
    ~Impl();
    Impl(const Impl&) = delete;
    Impl& operator=(const Impl&) = delete;

    // LegacyFixedListeners: route its accepted connections to `session`, and keep upstreams off `ports`.
    void attach_legacy(SessionId session, std::vector<Port> ports);
    void detach_legacy();

    std::shared_ptr<FrontCore> core;
    std::shared_ptr<StrandRoutes> routes;
    std::shared_ptr<boost::asio::ip::tcp::acceptor> acceptor;
    std::optional<Port> port;
};

// Closes `acceptor` on its own executor, then runs `then` there.
void close_acceptor(const std::shared_ptr<boost::asio::ip::tcp::acceptor>& acceptor, UniqueFunction<void()> then);

}  // namespace rb::front
