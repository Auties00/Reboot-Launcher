#pragma once

#include <atomic>
#include <cstdint>
#include <memory>

#include "reboot/client.h"

namespace rb::sdk {

// Followed from the ConnectionLost and Reconnected events the context's subscriptions see.
enum class ConnectionState : std::uint8_t { Connected, Reconnecting, Closed };

namespace detail {

// The rb_ctx that a Context and its copies, Ops and Subscriptions share; the last owner destroys it,
// so none of them outlives it.
struct ContextState {
    explicit ContextState(rb_ctx* handle) noexcept : ctx(handle) {}
    ~ContextState() { rb_ctx_destroy(ctx); }
    ContextState(const ContextState&) = delete;
    ContextState& operator=(const ContextState&) = delete;

    rb_ctx* const ctx;
    std::atomic<ConnectionState> connection{ConnectionState::Connected};
};

using SharedContext = std::shared_ptr<ContextState>;

}  // namespace detail

}  // namespace rb::sdk
