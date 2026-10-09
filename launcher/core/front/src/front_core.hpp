#pragma once

#include <chrono>
#include <memory>
#include <optional>
#include <utility>

#include <boost/asio/any_io_executor.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/post.hpp>
#include <boost/asio/ssl/context.hpp>
#include <boost/asio/steady_timer.hpp>

#include "reboot/foundation/executor.hpp"
#include "reboot/foundation/function.hpp"
#include "reboot/foundation/types.hpp"
#include "reboot/front/session_front.hpp"
#include "route_table.hpp"

namespace rb::ports {
class ILoopbackPeerInspector;
}

namespace rb::front {

class StrandRoutes;

// One accepted client connection; RouteTable keeps them so routes and stop() can close them.
class FrontConnection {
public:
    virtual ~FrontConnection() = default;
    // Thread-safe: aborts whatever is open.
    virtual void abort() = 0;
    // Thread-safe: closes now when idle, else once the current exchange ends.
    virtual void drain() = 0;
};

// What the session listener, the fixed listeners and every connection share. References outlive the
// SessionFront; `routes` is strand-only and gone once the SessionFront is.
struct FrontCore {
    FrontCore(boost::asio::io_context& io_in, Executor& strand_in, TimerService& timers_in, WorkerPool& workers_in,
              ports::ILoopbackPeerInspector& peers_in, FrontOptions options_in)
        : io(io_in), strand(strand_in), timers(timers_in), workers(workers_in), peers(peers_in),
          options(std::move(options_in)) {}

    boost::asio::io_context& io;
    Executor& strand;
    TimerService& timers;
    WorkerPool& workers;
    ports::ILoopbackPeerInspector& peers;
    const FrontOptions options;
    // Set by SessionFront::start before the first accept.
    std::unique_ptr<boost::asio::ssl::context> tls;
    RouteTable table;
    std::weak_ptr<StrandRoutes> routes;
};

// One TimerService timer at a time, armed and cancelled from a connection's executor, where the expiry
// also runs. A later arm or cancel voids an expiry already on its way.
class Deadline {
public:
    Deadline(std::shared_ptr<FrontCore> core, boost::asio::any_io_executor executor);
    ~Deadline();
    Deadline(const Deadline&) = delete;
    Deadline& operator=(const Deadline&) = delete;

    void arm(std::chrono::milliseconds delay, UniqueFunction<void()> on_expire);
    void cancel();

private:
    struct Slot;
    struct State {
        // Only the executor side holds it, so no strand-side task outlives the io_context with it.
        boost::asio::any_io_executor executor;
        u64 generation = 0;
        bool armed = false;
        // The slot then holds a handle, which only the strand may touch.
        bool used = false;
        UniqueFunction<void()> on_expire;
    };

    std::shared_ptr<FrontCore> core_;
    // Strand-only.
    std::shared_ptr<Slot> slot_;
    // Executor-only.
    std::shared_ptr<State> state_;
};

class Abandonable {
public:
    virtual ~Abandonable() = default;
    // On the waiter's executor: the wait ends without a value.
    virtual void abandon() = 0;
};

// A value another thread delivers to a coroutine; the waiter can give up, and a late value is dropped.
template <class T>
class Pending final : public Abandonable, public std::enable_shared_from_this<Pending<T>> {
public:
    explicit Pending(const boost::asio::any_io_executor& executor)
        : signal_(executor, boost::asio::steady_timer::time_point::max()) {}

    // Callable once, from any thread. `dropped` gets a value that arrives after the waiter gave up.
    [[nodiscard]] UniqueFunction<void(T)> resolver(UniqueFunction<void(T)> dropped = nullptr) {
        return [self = this->shared_from_this(), dropped = std::move(dropped)](T value) mutable {
            boost::asio::post(self->signal_.get_executor(), [self, dropped = std::move(dropped),
                                                             value = std::move(value)]() mutable {
                if (self->settled_) {
                    if (dropped) dropped(std::move(value));
                    return;
                }
                self->value_ = std::move(value);
                self->settled_ = true;
                self->signal_.cancel();
            });
        };
    }

    void abandon() override {
        settled_ = true;
        signal_.cancel();
    }

    [[nodiscard]] bool settled() const noexcept { return settled_; }
    [[nodiscard]] boost::asio::steady_timer& signal() noexcept { return signal_; }
    [[nodiscard]] std::optional<T> take() { return std::exchange(value_, std::nullopt); }

private:
    // Never expires; cancelling it wakes the waiter.
    boost::asio::steady_timer signal_;
    std::optional<T> value_;
    bool settled_ = false;
};

// Serves `socket`, accepted by a listener of `kind` on `port`; the table refuses it while stopping.
void serve_connection(const std::shared_ptr<FrontCore>& core, boost::asio::ip::tcp::socket socket, ListenerKind kind,
                      Port port);

// Accepts until `acceptor` is closed.
void accept_connections(const std::shared_ptr<FrontCore>& core,
                        const std::shared_ptr<boost::asio::ip::tcp::acceptor>& acceptor, ListenerKind kind, Port port);

// 127.0.0.1:`port` (0 for any), exclusive on Windows so no later socket shares it.
[[nodiscard]] std::shared_ptr<boost::asio::ip::tcp::acceptor> bind_loopback(boost::asio::io_context& io, Port port,
                                                                          boost::system::error_code& error);

}  // namespace rb::front
