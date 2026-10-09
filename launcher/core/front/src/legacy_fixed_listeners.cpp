#include "reboot/front/legacy_fixed_listeners.hpp"

#include <string>
#include <utility>

#include <boost/asio/error.hpp>
#include <boost/asio/ip/tcp.hpp>

#include "front_core.hpp"
#include "messages.hpp"
#include "reboot/foundation/events.hpp"
#include "reboot/foundation/executor.hpp"
#include "reboot/front/xmpp_unavailable.hpp"
#include "reboot/net/port_conflict.hpp"
#include "reboot/net/port_owner_service.hpp"
#include "reboot/net/port_protocol.hpp"
#include "session_front_impl.hpp"

namespace rb::front {

namespace {

namespace asio = boost::asio;
using tcp = asio::ip::tcp;

constexpr IpAddress kLoopback = IpAddress::v4(0x7F000001);

[[nodiscard]] Diagnostic cancelled(SessionId session) {
    return make_diag(ErrorDomain::Front, msg::kLegacyFixedCancelled)
        .arg("session", format_uuid(session.value))
        .kind(ErrorKind::Cancelled);
}

}  // namespace

// Strand-only.
struct LegacyFixedListeners::Impl {
    Impl(asio::io_context& io_in, Executor& strand_in, WorkerPool& workers_in, SessionFront& front_in,
         net::PortOwnerService& owners_in, EventBus& events_in, LegacyFixedPorts ports_in)
        : io(io_in), strand(strand_in), workers(workers_in), front(front_in), owners(owners_in), events(events_in),
          ports(ports_in) {}

    // The sockets close on the I/O side; a bind waits for them, so a quick reopen never sees its own ports busy.
    void close_sockets() {
        for (const std::shared_ptr<tcp::acceptor>& acceptor : acceptors) {
            ++closing;
            close_acceptor(acceptor, [&strand = strand, life = std::weak_ptr<int>(life), this] {
                strand.post([life, this] {
                    if (life.lock()) closed_one();
                });
            });
        }
        acceptors.clear();
    }

    void closed_one() {
        if (--closing != 0) return;
        std::vector<UniqueFunction<void()>> waiting = std::move(after_close);
        after_close.clear();
        for (UniqueFunction<void()>& resume : waiting) resume();
    }

    asio::io_context& io;
    Executor& strand;
    WorkerPool& workers;
    SessionFront& front;
    net::PortOwnerService& owners;
    EventBus& events;
    const LegacyFixedPorts ports;
    // Claimed from open() until the sockets close or a failed open reports.
    std::optional<SessionId> holder;
    u64 generation = 0;
    std::vector<std::shared_ptr<tcp::acceptor>> acceptors;
    std::size_t closing = 0;
    std::vector<UniqueFunction<void()>> after_close;
    // Lets late callbacks tell that the listeners are gone.
    std::shared_ptr<int> life = std::make_shared<int>(0);
};

LegacyFixedListeners::LegacyFixedListeners(asio::io_context& io, Executor& strand, WorkerPool& workers,
                                           SessionFront& front, net::PortOwnerService& owners, EventBus& events,
                                           LegacyFixedPorts ports)
    : impl_(std::make_unique<Impl>(io, strand, workers, front, owners, events, ports)) {}

LegacyFixedListeners::~LegacyFixedListeners() {
    if (!impl_->acceptors.empty()) {
        impl_->close_sockets();
        impl_->front.impl_->detach_legacy();
    }
}

Result<void> LegacyFixedListeners::open(LegacyFixedRequest request, CancelToken token,
                                        UniqueFunction<void(Result<void>)> done) {
    Impl& impl = *impl_;
    if (impl.holder)
        return make_diag(ErrorDomain::Front, msg::kLegacyFixedInUse)
            .arg("session", format_uuid(impl.holder->value))
            .kind(ErrorKind::Conflict)
            .fail();
    SessionFront::Impl& front = *impl.front.impl_;
    if (!front.routes->contains(request.session))
        return make_diag(ErrorDomain::Front, msg::kUnknownSession)
            .arg("session", format_uuid(request.session.value))
            .kind(ErrorKind::NotFound)
            .fail();

    const SessionId session = request.session;
    impl.holder = session;
    const u64 generation = ++impl.generation;

    UniqueFunction<void()> bind = [&impl, &front, request = std::move(request), token = std::move(token),
                                   done = std::move(done), session, generation]() mutable {
        // close() for this session came first.
        if (impl.generation != generation) {
            done(std::unexpected(cancelled(session)));
            return;
        }
        if (token.cancelled()) {
            impl.holder.reset();
            done(std::unexpected(cancelled(session)));
            return;
        }
        std::vector<Port> ports{impl.ports.http};
        if (request.xmpp) ports.push_back(impl.ports.xmpp);
        std::vector<std::shared_ptr<tcp::acceptor>> bound;
        for (const Port port : ports) {
            boost::system::error_code error;
            std::shared_ptr<tcp::acceptor> acceptor = bind_loopback(impl.io, port, error);
            if (acceptor) {
                bound.push_back(std::move(acceptor));
                continue;
            }
            // Nothing accepts on them yet, so they close right here.
            for (const std::shared_ptr<tcp::acceptor>& opened : bound) {
                boost::system::error_code ignored;
                opened->close(ignored);
            }
            const Endpoint bind_to{kLoopback, port};
            const bool in_use = error == asio::error::address_in_use;
            if (!in_use && error != asio::error::access_denied) {
                impl.holder.reset();
                done(make_diag(ErrorDomain::Front, msg::kListenFailed)
                         .arg("address", bind_to.to_string())
                         .os(SystemError{SystemError::Origin::Host, error.value()})
                         .fail());
                return;
            }
            // Windows refuses an exclusive bind over a held port as access denied, so the owners decide.
            impl.workers.submit<std::vector<net::PortOwnerInfo>>(
                [&owners = impl.owners, bind_to, ours = std::move(request.ours)](CancelToken) {
                    return owners.owners(net::PortProtocol::Tcp, bind_to, ours);
                },
                token, impl.strand,
                [&impl, life = std::weak_ptr<int>(impl.life), generation, session, bind_to, in_use, token,
                 done = std::move(done)](Result<std::vector<net::PortOwnerInfo>> owners) mutable {
                    if (life.lock() && impl.generation == generation) impl.holder.reset();
                    if (token.cancelled()) {
                        done(std::unexpected(cancelled(session)));
                        return;
                    }
                    net::PortConflict conflict;
                    conflict.protocol = net::PortProtocol::Tcp;
                    conflict.bind = bind_to;
                    if (owners)
                        conflict.owners = std::move(*owners);
                    else
                        conflict.lookup_error = std::move(owners.error());
                    conflict.kind = in_use || !conflict.owners.empty() ? net::PortConflictKind::InUse
                                                                       : net::PortConflictKind::AccessDenied;
                    done(std::unexpected(net::to_diagnostic(conflict)));
                });
            return;
        }

        impl.acceptors = std::move(bound);
        front.attach_legacy(session, ports);
        for (std::size_t i = 0; i < ports.size(); ++i)
            accept_connections(front.core, impl.acceptors[i], ListenerKind::Fixed, ports[i]);
        if (!request.xmpp)
            impl.events.publish(EventKind::XmppUnavailable,
                                XmppUnavailable{session, XmppUnavailableReason::ThirdPartyAuthDll},
                                EventScope{session, std::nullopt, {}});
        done(Result<void>{});
    };

    // `done` never runs inside open().
    if (impl.closing != 0)
        impl.after_close.push_back(std::move(bind));
    else
        impl.strand.post([life = std::weak_ptr<int>(impl.life), bind = std::move(bind)]() mutable {
            if (life.lock()) bind();
        });
    return {};
}

void LegacyFixedListeners::close(SessionId session) {
    Impl& impl = *impl_;
    if (impl.holder != session) return;
    impl.holder.reset();
    ++impl.generation;
    if (impl.acceptors.empty()) return;
    impl.close_sockets();
    impl.front.impl_->detach_legacy();
}

std::optional<SessionId> LegacyFixedListeners::holder() const {
    if (impl_->acceptors.empty()) return std::nullopt;
    return impl_->holder;
}

}  // namespace rb::front
