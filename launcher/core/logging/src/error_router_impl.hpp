#pragma once

#include <deque>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

#include "reboot/foundation/function.hpp"
#include "reboot/logging/error_router.hpp"

namespace rb::logging {

struct ErrorRouter::Impl {
    // Shared with posted sink failures, which may run after the router is gone.
    struct Link {
        // Strand-only; null once the router is gone.
        ErrorRouter* owner = nullptr;
    };

    Impl(Executor& strand_ref, const IClock& clock_ref) : strand(strand_ref), clock(clock_ref) {}

    LogRef log(LogLevel level, LogCategory category, const std::optional<SessionId>& session, std::string_view text);
    void notify(BackgroundFailureId id, std::optional<BackgroundFailure> failure);

    Executor& strand;
    const IClock& clock;
    std::shared_ptr<Link> link = std::make_shared<Link>();
    // Set by watch(), which lives in its own file so routers that never watch need no FileLogSink.
    UniqueFunction<std::string()> current_file_name;
    UniqueFunction<void()> unwatch;
    UniqueFunction<void(const BackgroundFailureChange&)> on_change;
    std::deque<BackgroundFailure> failures;
    u64 next_failure_id = 1;
    u64 next_ref = 1;
};

}  // namespace rb::logging
