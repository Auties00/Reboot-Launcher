#pragma once

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <functional>
#include <limits>
#include <memory>
#include <mutex>
#include <optional>
#include <utility>

#include "reboot/api/codec.hpp"
#include "reboot/api/v1/common.hpp"
#include "reboot/client.h"
#include "reboot/sdk/buffer.hpp"
#include "reboot/sdk/connection.hpp"
#include "reboot/sdk/dispatcher.hpp"
#include "reboot/sdk/error.hpp"
#include "reboot/sdk/event.hpp"

namespace rb::sdk {

namespace detail {

// What one rb_subscribe needs; shared by its Subscription and the tasks its wake posts.
class SubscriptionState : public std::enable_shared_from_this<SubscriptionState> {
public:
    SubscriptionState(SharedContext context, std::uint64_t sub) : context_(std::move(context)), sub_(sub) {}
    // Waits for a running wake, after which none runs.
    ~SubscriptionState() { rb_unsubscribe(context_->ctx, sub_); }
    SubscriptionState(const SubscriptionState&) = delete;
    SubscriptionState& operator=(const SubscriptionState&) = delete;

    [[nodiscard]] std::uint64_t id() const noexcept { return sub_; }
    [[nodiscard]] std::uint64_t skipped() const noexcept { return skipped_.load(); }
    [[nodiscard]] bool closed() const noexcept { return closed_.load(); }

    Expected<std::optional<Event>> next(std::chrono::milliseconds timeout) {
        const auto deadline = std::chrono::steady_clock::now() + timeout;
        for (;;) {
            const auto left = std::max(std::chrono::duration_cast<std::chrono::milliseconds>(
                                           deadline - std::chrono::steady_clock::now()),
                                       std::chrono::milliseconds::zero());
            Buffer buffer;
            const rb_status status = rb_events_next(context_->ctx, sub_, wait_ms(left), buffer.out());
            if (status == RB_PENDING) return std::nullopt;
            if (status != RB_OK) {
                mark_closed();
                return std::unexpected(Error::last(status));
            }
            if (std::optional<Event> event = read(buffer)) return event;
        }
    }

    void deliver(Dispatcher& dispatcher, std::function<void(const Event&)> handler) {
        {
            const std::scoped_lock lock(mutex_);
            dispatcher_ = &dispatcher;
            handler_ = std::move(handler);
        }
        rb_events_set_wake(context_->ctx, sub_, &SubscriptionState::wake, reinterpret_cast<std::uintptr_t>(this));
    }

private:
    [[nodiscard]] static std::uint32_t wait_ms(std::chrono::milliseconds timeout) noexcept {
        constexpr auto kMax = std::chrono::milliseconds{std::numeric_limits<std::uint32_t>::max() - 1};
        return static_cast<std::uint32_t>(std::min(timeout, kMax).count());
    }

    // On a reboot_client thread, which must not block.
    static void wake(std::uintptr_t user) {
        auto* self = reinterpret_cast<SubscriptionState*>(user);
        Dispatcher* dispatcher = nullptr;
        {
            const std::scoped_lock lock(self->mutex_);
            dispatcher = self->dispatcher_;
        }
        if (dispatcher == nullptr) return;
        dispatcher->post([weak = self->weak_from_this()] {
            if (const std::shared_ptr<SubscriptionState> state = weak.lock()) state->drain();
        });
    }

    // Pops everything queued, which re-arms the wake.
    void drain() {
        for (;;) {
            Buffer buffer;
            const rb_status status = rb_events_next(context_->ctx, sub_, 0, buffer.out());
            if (status == RB_PENDING) return;
            if (status != RB_OK) {
                if (!closed_.load()) {
                    mark_closed();
                    dispatch(Event{});
                }
                return;
            }
            if (std::optional<Event> event = read(buffer)) dispatch(*event);
        }
    }

    void dispatch(const Event& event) {
        std::function<void(const Event&)> handler;
        {
            const std::scoped_lock lock(mutex_);
            handler = handler_;
        }
        if (handler) handler(event);
    }

    std::optional<Event> read(const Buffer& buffer) {
        std::optional<Event> event = decode_event(buffer.bytes());
        if (!event) {
            skipped_.fetch_add(1);
            return std::nullopt;
        }
        if (event->get<ConnectionLost>() != nullptr) context_->connection.store(ConnectionState::Reconnecting);
        if (event->get<Reconnected>() != nullptr) context_->connection.store(ConnectionState::Connected);
        return event;
    }

    // A subscription ends for good only when its context closed or will not reconnect.
    void mark_closed() {
        closed_.store(true);
        context_->connection.store(ConnectionState::Closed);
    }

    SharedContext context_;
    const std::uint64_t sub_;
    std::mutex mutex_;
    Dispatcher* dispatcher_ = nullptr;
    std::function<void(const Event&)> handler_;
    std::atomic<std::uint64_t> skipped_{0};
    std::atomic<bool> closed_{false};
};

}  // namespace detail

// An event stream. Pull it with next(), or have deliver() push it through a Dispatcher. Events this
// build cannot read are skipped and counted.
class Subscription {
public:
    [[nodiscard]] static Expected<Subscription> open(const detail::SharedContext& context,
                                                     const api::EventFilter& filter = {}) {
        const api::Bytes encoded = api::encode(filter);
        std::uint64_t sub = 0;
        const rb_status status = rb_subscribe(context->ctx, encoded.data(), encoded.size(), &sub);
        if (status != RB_OK) return std::unexpected(Error::last(status));
        return Subscription(std::make_shared<detail::SubscriptionState>(context, sub));
    }

    [[nodiscard]] std::uint64_t id() const noexcept { return state_->id(); }

    // The next event, nullopt when none arrived within `timeout`; RB_E_CLOSED once the subscription
    // has ended and is drained.
    [[nodiscard]] Expected<std::optional<Event>> next(std::chrono::milliseconds timeout) const {
        return state_->next(timeout);
    }

    // From now on `dispatcher` runs `handler` for every event, woken by reboot_client. When the
    // subscription ends the handler gets one Event holding Closed. `dispatcher` must outlive this.
    void deliver(Dispatcher& dispatcher, std::function<void(const Event&)> handler) {
        state_->deliver(dispatcher, std::move(handler));
    }

    [[nodiscard]] std::uint64_t skipped() const noexcept { return state_->skipped(); }
    [[nodiscard]] bool closed() const noexcept { return state_->closed(); }

private:
    explicit Subscription(std::shared_ptr<detail::SubscriptionState> state) : state_(std::move(state)) {}

    std::shared_ptr<detail::SubscriptionState> state_;
};

}  // namespace rb::sdk
