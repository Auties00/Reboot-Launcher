#pragma once

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <utility>
#include <variant>

#include "reboot/api/codec.hpp"
#include "reboot/api/v1/common.hpp"
#include "reboot/client.h"
#include "reboot/sdk/buffer.hpp"
#include "reboot/sdk/connection.hpp"
#include "reboot/sdk/error.hpp"
#include "reboot/sdk/event.hpp"
#include "reboot/sdk/subscription.hpp"

namespace rb::sdk {

// Whether an operation survives its connection; Default is the method's own policy.
enum class Disconnect : std::uint8_t { Default, Bound, Detached };

struct Cancelled {
    api::CancelReason reason{};
};

struct TimedOut {
    // The phase that ran out of time.
    std::string phase;
};

// How an operation ended: its response, the engine's Diagnostic, a cancel or a deadline.
template <class T>
class Outcome {
public:
    using Value = std::variant<T, Error, Cancelled, TimedOut>;

    explicit Outcome(Value value) : value_(std::move(value)) {}

    [[nodiscard]] bool completed() const noexcept { return std::holds_alternative<T>(value_); }
    [[nodiscard]] const T* value() const noexcept { return std::get_if<T>(&value_); }
    [[nodiscard]] const Error* error() const noexcept { return std::get_if<Error>(&value_); }
    [[nodiscard]] const Cancelled* cancelled() const noexcept { return std::get_if<Cancelled>(&value_); }
    [[nodiscard]] const TimedOut* timed_out() const noexcept { return std::get_if<TimedOut>(&value_); }
    [[nodiscard]] const Value& get() const noexcept { return value_; }

    // The response, or an Error for every other end (sdk.cancelled and sdk.timed_out for those two).
    [[nodiscard]] Expected<T> into_expected() && {
        if (T* done = std::get_if<T>(&value_)) return std::move(*done);
        if (Error* failed = std::get_if<Error>(&value_)) return std::unexpected(std::move(*failed));
        if (const auto* timed_out = std::get_if<TimedOut>(&value_))
            return std::unexpected(Error::local(RB_E_REMOTE, "sdk.timed_out", {{"phase", api::ArgKind::String, timed_out->phase}}));
        return std::unexpected(Error::local(RB_E_REMOTE, "sdk.cancelled", {}, api::ErrorKind::Cancelled));
    }

private:
    Value value_;
};

// An operation attached to this client: watch its progress, cancel it, read its outcome. Destroying
// it releases the handle; the operation itself runs on as its disconnect policy says. One thread at
// a time.
template <class T>
class Op {
public:
    Op(detail::SharedContext context, std::uint64_t id, std::uint32_t method)
        : context_(std::move(context)), id_(id), method_(method) {}
    ~Op() {
        if (context_) rb_op_release(context_->ctx, id_);
    }
    Op(Op&&) noexcept = default;
    Op& operator=(Op&& other) noexcept {
        if (this != &other) {
            if (context_) rb_op_release(context_->ctx, id_);
            context_ = std::move(other.context_);
            id_ = other.id_;
            method_ = other.method_;
            progress_ = std::move(other.progress_);
        }
        return *this;
    }
    Op(const Op&) = delete;
    Op& operator=(const Op&) = delete;

    [[nodiscard]] std::uint64_t id() const noexcept { return id_; }
    [[nodiscard]] std::uint32_t method() const noexcept { return method_; }
    // The last Progress observe() or wait() saw.
    [[nodiscard]] const std::optional<api::Progress>& progress() const noexcept { return progress_; }

    // Idempotent; the outcome still arrives, normally as Cancelled.
    Expected<void> cancel() const {
        const rb_status status = rb_op_cancel(context_->ctx, id_);
        if (status != RB_OK) return std::unexpected(Error::last(status));
        return {};
    }

    // The outcome, or nullopt while the operation runs.
    [[nodiscard]] Expected<std::optional<Outcome<T>>> poll() const {
        Buffer buffer;
        const rb_status status = rb_op_result(context_->ctx, id_, buffer.out());
        if (status == RB_PENDING) return std::nullopt;
        if (status != RB_OK) return std::unexpected(Error::last(status));
        std::expected<api::Outcome, api::DecodeError> outcome = api::decode<api::Outcome>(buffer.bytes());
        if (!outcome) return std::unexpected(Error::local(RB_E_REMOTE, "sdk.malformed_reply", {{"what", api::ArgKind::String, "outcome"}}));
        return std::optional<Outcome<T>>(read(std::move(*outcome)));
    }

    // Blocks until the operation ends or `timeout` passes (nullopt), handing each Progress to
    // `on_progress` on this thread.
    [[nodiscard]] Expected<std::optional<Outcome<T>>> wait(std::chrono::milliseconds timeout,
                                                           const std::function<void(const api::Progress&)>& on_progress = {}) {
        api::EventFilter filter;
        filter.kinds = {api::EventKind::OpProgress, api::EventKind::OpCompleted};
        filter.op_id = id_;
        Expected<Subscription> events = Subscription::open(context_, filter);
        if (!events) return std::unexpected(std::move(events.error()));
        const auto deadline = std::chrono::steady_clock::now() + timeout;
        for (;;) {
            // Polled as well, since an outcome may have arrived before the subscription.
            Expected<std::optional<Outcome<T>>> outcome = poll();
            if (!outcome || *outcome) return outcome;
            const auto left = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - std::chrono::steady_clock::now());
            if (left <= std::chrono::milliseconds::zero()) return std::nullopt;
            Expected<std::optional<Event>> event = events->next(std::min(left, kPollInterval));
            if (!event) return poll();
            if (!*event || !observe(**event)) continue;
            if (const auto* progress = (*event)->get<api::Progress>(); progress != nullptr && on_progress)
                on_progress(*progress);
        }
    }

    // Takes this operation's Progress from an event seen on any subscription; true when it was this op's.
    bool observe(const Event& event) {
        if (event.op_id != id_) return false;
        if (const auto* progress = event.get<api::Progress>()) progress_ = *progress;
        return true;
    }

private:
    static constexpr std::chrono::milliseconds kPollInterval{250};

    [[nodiscard]] static Outcome<T> read(api::Outcome outcome) {
        if (outcome.completed) {
            std::expected<T, api::DecodeError> response = api::decode<T>(*outcome.completed);
            if (!response)
                return Outcome<T>(Error::local(RB_E_REMOTE, "sdk.malformed_reply", {{"what", api::ArgKind::String, "response"}}));
            return Outcome<T>(std::move(*response));
        }
        if (outcome.failed) return Outcome<T>(Error(RB_E_REMOTE, std::move(*outcome.failed)));
        if (outcome.cancelled) return Outcome<T>(Cancelled{*outcome.cancelled});
        return Outcome<T>(TimedOut{outcome.timed_out.value_or(std::string{})});
    }

    detail::SharedContext context_;
    std::uint64_t id_ = 0;
    std::uint32_t method_ = 0;
    std::optional<api::Progress> progress_;
};

}  // namespace rb::sdk
