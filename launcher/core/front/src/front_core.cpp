#include "front_core.hpp"

#include <boost/asio/as_tuple.hpp>
#include <boost/asio/co_spawn.hpp>
#include <boost/asio/detail/socket_option.hpp>
#include <boost/asio/strand.hpp>
#include <boost/asio/use_awaitable.hpp>

#include "reboot/foundation/log.hpp"

namespace reboot::front {

namespace {

namespace asio = boost::asio;
using tcp = asio::ip::tcp;

#ifdef _WIN32
// Without it Windows lets a later SO_REUSEADDR socket take over the port.
using ExclusiveAddressUse = asio::detail::socket_option::boolean<SOL_SOCKET, SO_EXCLUSIVEADDRUSE>;
#endif

constexpr std::chrono::milliseconds kAcceptRetry{50};

asio::awaitable<void> accept_loop(std::shared_ptr<FrontCore> core, std::shared_ptr<tcp::acceptor> acceptor,
                                  ListenerKind kind, Port port) {
    for (;;) {
        auto [error, socket] =
            co_await acceptor->async_accept(asio::any_io_executor(asio::make_strand(core->io)),
                                            asio::as_tuple(asio::use_awaitable));
        if (!acceptor->is_open() || error == asio::error::operation_aborted) co_return;
        if (error) {
            // Out of descriptors, say: back off instead of spinning on the same failure.
            REBOOT_LOG_WARN(Net, "front: accepting on port {} failed ({})", port.value, error.value());
            asio::steady_timer pause(acceptor->get_executor(), kAcceptRetry);
            static_cast<void>(co_await pause.async_wait(asio::as_tuple(asio::use_awaitable)));
            continue;
        }
        serve_connection(core, std::move(socket), kind, port);
    }
}

}  // namespace

struct Deadline::Slot {
    TimerHandle handle;
};

Deadline::Deadline(std::shared_ptr<FrontCore> core, asio::any_io_executor executor)
    : core_(std::move(core)), slot_(std::make_shared<Slot>()), state_(std::make_shared<State>()) {
    state_->executor = std::move(executor);
}

Deadline::~Deadline() {
    if (!state_->used) return;
    // The handle is strand-only, so it is cancelled and dropped there.
    core_->strand.post([slot = std::move(slot_)] { slot->handle.cancel(); });
}

void Deadline::arm(std::chrono::milliseconds delay, UniqueFunction<void()> on_expire) {
    const u64 generation = ++state_->generation;
    state_->armed = true;
    state_->used = true;
    state_->on_expire = std::move(on_expire);
    core_->strand.post([slot = slot_, core = core_, delay, state = std::weak_ptr<State>(state_), generation] {
        slot->handle = core->timers.after(delay, [state, generation] {
            const std::shared_ptr<State> owner = state.lock();
            if (!owner) return;
            asio::post(owner->executor, [state, generation] {
                const std::shared_ptr<State> live = state.lock();
                if (!live || live->generation != generation) return;
                UniqueFunction<void()> expire = std::move(live->on_expire);
                live->on_expire = nullptr;
                live->armed = false;
                if (expire) expire();
            });
        });
    });
}

void Deadline::cancel() {
    ++state_->generation;
    state_->on_expire = nullptr;
    if (!std::exchange(state_->armed, false)) return;
    core_->strand.post([slot = slot_] { slot->handle.cancel(); });
}

std::shared_ptr<tcp::acceptor> bind_loopback(asio::io_context& io, Port port, boost::system::error_code& error) {
    auto acceptor = std::make_shared<tcp::acceptor>(asio::make_strand(io));
    const tcp::endpoint endpoint(asio::ip::address_v4::loopback(), port.value);
    acceptor->open(endpoint.protocol(), error);
    if (error) return nullptr;
#ifdef _WIN32
    acceptor->set_option(ExclusiveAddressUse(true), error);
#else
    // POSIX never lets a second listener share the port; this only skips TIME_WAIT leftovers.
    acceptor->set_option(asio::socket_base::reuse_address(true), error);
#endif
    if (!error) acceptor->bind(endpoint, error);
    if (!error) acceptor->listen(asio::socket_base::max_listen_connections, error);
    if (error) {
        boost::system::error_code ignored;
        acceptor->close(ignored);
        return nullptr;
    }
    return acceptor;
}

void accept_connections(const std::shared_ptr<FrontCore>& core, const std::shared_ptr<tcp::acceptor>& acceptor,
                        ListenerKind kind, Port port) {
    asio::co_spawn(acceptor->get_executor(), accept_loop(core, acceptor, kind, port), [port](std::exception_ptr error) {
        if (error) REBOOT_LOG_ERROR(Net, "front: the listener on port {} stopped on an exception", port.value);
    });
}

}  // namespace reboot::front
