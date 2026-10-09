#include "reboot/storage/reset_service.hpp"

#include <utility>

#include "messages.hpp"
#include "reboot/storage/settings.hpp"
#include "reboot/storage/settings_registry.hpp"

namespace rb::storage {

namespace {

[[nodiscard]] Outcome<ResetReport> outcome_of(Result<ResetReport> result) {
    if (!result) return Failed{std::move(result.error())};
    return Completed<ResetReport>{std::move(*result)};
}

[[nodiscard]] Diagnostic blocked(const ResetBlockers& blockers) {
    return make_diag(ErrorDomain::Storage, msg::kResetBlocked)
        .kind(ErrorKind::Conflict)
        .arg("sessions", blockers.sessions.size())
        .build();
}

}  // namespace

ResetService::ResetService(Settings& settings, const SettingsRegistry& registry, OpRegistry& ops, ResetHooks hooks)
    : settings_(settings), registry_(registry), ops_(ops), hooks_(std::move(hooks)) {}

Result<ResetReport> ResetService::reset(ResetGroup group) {
    const ResetBlockers blockers = hooks_.find_blockers(group);
    if (!blockers.empty()) return std::unexpected(blocked(blockers));
    return reset_unblocked(group);
}

Result<OpHandle> ResetService::start_reset_after_stop(ResetGroup group, DisconnectPolicy policy) {
    auto [handle, op] = ops_.create<ResetReport>(OpKind::Generic, policy, std::nullopt);
    ResetBlockers blockers = hooks_.find_blockers(group);
    if (blockers.empty()) {
        op.complete(outcome_of(reset_unblocked(group)));
        return handle;
    }
    op.progress(Progress{.phase = "stopping"});
    // The op's deadline cancels the token, so the stop answers well within the registry's retention.
    hooks_.stop_blockers(group, std::move(blockers), op.token(), [this, group, &op](Result<void> stopped) {
        // Cancelled or timed out meanwhile: reset nothing, but complete so the registry can free the op.
        if (op.done()) {
            op.complete(Cancelled{});
            return;
        }
        if (!stopped) {
            op.complete(Failed{make_diag(ErrorDomain::Storage, msg::kResetStopFailed)
                                   .kind(ErrorKind::Conflict)
                                   .cause(std::move(stopped.error()))
                                   .build()});
            return;
        }
        // Something may have started again while the rest stopped.
        if (const ResetBlockers again = hooks_.find_blockers(group); !again.empty()) {
            op.complete(Failed{blocked(again)});
            return;
        }
        op.complete(outcome_of(reset_unblocked(group)));
    });
    return handle;
}

Result<ResetReport> ResetService::reset_unblocked(ResetGroup group) {
    if (Result<void> records = hooks_.reset_records(group); !records) return std::unexpected(records.error());
    const std::vector<const AnyKey*> keys = registry_.in_group(group);
    Result<u64> revision = settings_.reset_to_defaults(keys);
    if (!revision) return std::unexpected(std::move(revision.error()));
    ResetReport report{.revision = *revision, .keys = {}};
    for (const AnyKey* key : keys) report.keys.emplace_back(key->spec().id);
    return report;
}

}  // namespace rb::storage
