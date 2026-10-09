#pragma once

#include <chrono>
#include <cstddef>
#include <memory>
#include <optional>
#include <vector>

#include "reboot/foundation/diag.hpp"
#include "reboot/foundation/function.hpp"
#include "reboot/foundation/log.hpp"
#include "reboot/foundation/operation.hpp"
#include "reboot/foundation/types.hpp"

namespace rb {
class Executor;
class IClock;
}  // namespace rb

namespace rb::logging {

class FileLogSink;

inline constexpr std::size_t kMaxBackgroundFailures = 32;

using BackgroundFailureId = Counter<struct BackgroundFailureTag, u64>;

// A failure that belongs to no op, such as a startup step or a log file; kept until acknowledged.
struct BackgroundFailure {
    BackgroundFailureId id;
    // The newest occurrence.
    Diagnostic diag;
    LogCategory category{};
    std::optional<SessionId> session;
    std::chrono::system_clock::time_point first_at;
    std::chrono::system_clock::time_point last_at;
    u32 occurrences = 1;
};

// `failure` is absent once the entry was acknowledged or dropped.
struct BackgroundFailureChange {
    BackgroundFailureId id;
    std::optional<BackgroundFailure> failure;
};

// Capabilities: logging-diagnostics.log-and-errors, logging-diagnostics.+41, logging-diagnostics.+51.
// Strand-only. The one place failures are logged; call sites never log their own.
// Each line carries ref=<n>, and the diagnostic's log_ref is {session part name, n}.
class ErrorRouter {
public:
    ErrorRouter(Executor& strand, const IClock& clock);
    // Unhooks the watched sink; failures it posted and not yet run are dropped.
    ~ErrorRouter();
    ErrorRouter(const ErrorRouter&) = delete;
    ErrorRouter& operator=(const ErrorRouter&) = delete;

    // Reports the sink's part and Wine log write failures; log_ref names its current part.
    void watch(FileLogSink& session_log);

    // Replays every kept failure, then each change; the engine shows them as notices.
    void set_on_change(UniqueFunction<void(const BackgroundFailureChange&)> on_change);

    // A repeat matches an unacknowledged entry's id, args, os_error, category and session.
    // It counts there and replaces `diag`; detail and causes may differ. Every occurrence is logged.
    void report(Diagnostic diag, LogCategory category, std::optional<SessionId> session);

    // Oldest first; past kMaxBackgroundFailures the oldest is dropped.
    [[nodiscard]] std::vector<BackgroundFailure> background_failures() const;
    // Unknown ids are ignored, so two UIs may acknowledge the same failure.
    void acknowledge(BackgroundFailureId id);

    // OpRegistry's hook before it publishes OpCompleted: logs Failed and TimedOut, sets Failed's log_ref.
    void record_outcome(OpId op, OpKind kind, const std::optional<SessionId>& session, ErasedOutcome& outcome);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace rb::logging
