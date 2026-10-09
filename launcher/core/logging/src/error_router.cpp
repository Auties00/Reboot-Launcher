#include "reboot/logging/error_router.hpp"

#include <algorithm>
#include <format>
#include <limits>
#include <utility>
#include <variant>

#include "error_router_impl.hpp"
#include "reboot/foundation/clock.hpp"
#include "reboot/logging/log_format.hpp"

namespace rb::logging {

namespace {

[[nodiscard]] bool is_repeat(const BackgroundFailure& kept, const Diagnostic& diag, LogCategory category,
                             const std::optional<SessionId>& session) {
    return kept.diag.id == diag.id && kept.diag.args == diag.args && kept.diag.os_error == diag.os_error &&
           kept.category == category && kept.session == session;
}

}  // namespace

LogRef ErrorRouter::Impl::log(LogLevel level, LogCategory category, const std::optional<SessionId>& session,
                              std::string_view text) {
    const u64 ref = next_ref++;
    Logger::write(level, category, session, std::format("ref={} {}", ref, text));
    return LogRef{current_file_name ? current_file_name() : std::string(), ref};
}

void ErrorRouter::Impl::notify(BackgroundFailureId id, std::optional<BackgroundFailure> failure) {
    if (on_change) on_change(BackgroundFailureChange{id, std::move(failure)});
}

ErrorRouter::ErrorRouter(Executor& strand, const IClock& clock) : impl_(std::make_unique<Impl>(strand, clock)) {
    impl_->link->owner = this;
}

ErrorRouter::~ErrorRouter() {
    if (impl_->unwatch) impl_->unwatch();
    impl_->link->owner = nullptr;
}

void ErrorRouter::set_on_change(UniqueFunction<void(const BackgroundFailureChange&)> on_change) {
    impl_->on_change = std::move(on_change);
    for (const BackgroundFailure& failure : impl_->failures) impl_->notify(failure.id, failure);
}

void ErrorRouter::report(Diagnostic diag, LogCategory category, std::optional<SessionId> session) {
    diag.log_ref = impl_->log(level_for(diag.severity), category, session, "failure " + format_diagnostic(diag));
    const auto now = impl_->clock.system_now();

    const auto repeat = std::ranges::find_if(
        impl_->failures, [&](const BackgroundFailure& kept) { return is_repeat(kept, diag, category, session); });
    if (repeat != impl_->failures.end()) {
        if (repeat->occurrences < std::numeric_limits<u32>::max()) ++repeat->occurrences;
        repeat->last_at = now;
        repeat->diag = std::move(diag);
        impl_->notify(repeat->id, *repeat);
        return;
    }

    if (impl_->failures.size() == kMaxBackgroundFailures) {
        const BackgroundFailureId dropped = impl_->failures.front().id;
        impl_->failures.pop_front();
        impl_->notify(dropped, std::nullopt);
    }
    BackgroundFailure& added = impl_->failures.emplace_back(BackgroundFailure{
        .id = BackgroundFailureId{impl_->next_failure_id++},
        .diag = std::move(diag),
        .category = category,
        .session = std::move(session),
        .first_at = now,
        .last_at = now,
        .occurrences = 1,
    });
    impl_->notify(added.id, added);
}

std::vector<BackgroundFailure> ErrorRouter::background_failures() const {
    return {impl_->failures.begin(), impl_->failures.end()};
}

void ErrorRouter::acknowledge(BackgroundFailureId id) {
    const auto it = std::ranges::find(impl_->failures, id, &BackgroundFailure::id);
    if (it == impl_->failures.end()) return;
    impl_->failures.erase(it);
    impl_->notify(id, std::nullopt);
}

void ErrorRouter::record_outcome(OpId op, OpKind kind, const std::optional<SessionId>& session,
                                 ErasedOutcome& outcome) {
    const auto kind_number = static_cast<unsigned>(kind);
    if (auto* failed = std::get_if<Failed>(&outcome)) {
        failed->error.log_ref =
            impl_->log(level_for(failed->error.severity), LogCategory::Engine, session,
                       std::format("op {} kind={} failed {}", op.value, kind_number, format_diagnostic(failed->error)));
    } else if (const auto* timed_out = std::get_if<TimedOut>(&outcome)) {
        impl_->log(LogLevel::Warn, LogCategory::Engine, session,
                   std::format("op {} kind={} timed out in phase \"{}\"", op.value, kind_number, timed_out->phase));
    }
}

}  // namespace rb::logging
