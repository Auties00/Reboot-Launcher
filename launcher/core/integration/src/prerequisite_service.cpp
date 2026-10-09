#include "reboot/integration/prerequisite_service.hpp"

#include <algorithm>
#include <chrono>
#include <optional>
#include <utility>

#include "reboot/foundation/events.hpp"
#include "reboot/foundation/executor.hpp"
#include "reboot/integration/prerequisite_error.hpp"
#include "reboot/integration/prerequisite_spec.hpp"
#include "reboot/integration/prerequisites_changed.hpp"
#include "reboot/ports/os_services.hpp"

namespace reboot::integration {

namespace {

constexpr std::chrono::minutes kRemediateDeadline{10};

// What a remediation pass hands back to the strand.
struct RemediationPass {
    // Set once the remedy ran and the probe was asked again.
    std::optional<std::vector<Prerequisite>> rechecked;
    std::optional<Prerequisite> result;
    std::optional<Diagnostic> failure;
};

// In id order, without ids this build does not know.
[[nodiscard]] std::vector<Prerequisite> to_prerequisites(const std::vector<ports::PrerequisiteStatus>& statuses) {
    std::vector<Prerequisite> out;
    for (const ports::PrerequisiteStatus& status : statuses) {
        const std::optional<PrerequisiteId> id = parse_prerequisite_id(status.id);
        if (!id || std::ranges::find(out, *id, &Prerequisite::id) != out.end()) continue;
        const PrerequisiteSpec& spec = prerequisite_spec(*id);
        out.push_back(Prerequisite{.id = *id,
                                   .met = status.met,
                                   .impact = spec.impact,
                                   .remedy = spec.remedy,
                                   .guidance = spec.guidance,
                                   .platform_hint = status.remediation_message_id});
    }
    std::ranges::sort(out, {}, &Prerequisite::id);
    return out;
}

[[nodiscard]] const Prerequisite* find(const std::vector<Prerequisite>& prerequisites, PrerequisiteId id) {
    const auto it = std::ranges::find(prerequisites, id, &Prerequisite::id);
    return it == prerequisites.end() ? nullptr : &*it;
}

[[nodiscard]] Diagnostic error_of(PrerequisiteErrorCode code, PrerequisiteId id,
                                  std::optional<Diagnostic> cause = std::nullopt) {
    return to_diagnostic(PrerequisiteError{.code = code, .text = {}, .id = id, .cause = std::move(cause)});
}

[[nodiscard]] RemediationPass remediate(ports::IPrerequisiteProbe& probe, PrerequisiteId id) {
    RemediationPass pass;
    const std::vector<Prerequisite> before = to_prerequisites(probe.check());
    const Prerequisite* found = find(before, id);
    if (found == nullptr) {
        pass.failure = error_of(PrerequisiteErrorCode::NotApplicable, id);
        return pass;
    }
    const bool opens_settings = found->remedy == Remedy::OpenSettings;
    // The user asked to see the pane; anything else already met needs nothing.
    if (found->met && !opens_settings) {
        pass.result = *found;
        return pass;
    }
    if (Result<void> ran = probe.remediate(to_string(id)); !ran) {
        pass.failure = error_of(PrerequisiteErrorCode::RemediationFailed, id, std::move(ran.error()));
        return pass;
    }
    pass.rechecked = to_prerequisites(probe.check());
    const Prerequisite* after = find(*pass.rechecked, id);
    if (after == nullptr || (!after->met && !opens_settings)) {
        pass.failure = error_of(PrerequisiteErrorCode::StillMissing, id);
        return pass;
    }
    pass.result = *after;
    return pass;
}

}  // namespace

PrerequisiteService::PrerequisiteService(ports::IPrerequisiteProbe& probe, WorkerPool& workers, Executor& strand,
                                         OpRegistry& ops, EventBus& events)
    : probe_(probe), workers_(workers), strand_(strand), ops_(ops), events_(events) {}

void PrerequisiteService::check(CancelToken token, UniqueFunction<void(std::vector<Prerequisite>)> done) {
    workers_.submit<std::vector<Prerequisite>>(
        [&probe = probe_](CancelToken cancel) -> Result<std::vector<Prerequisite>> {
            if (cancel.cancelled()) return std::vector<Prerequisite>{};
            return to_prerequisites(probe.check());
        },
        std::move(token), strand_,
        [done = std::move(done)](Result<std::vector<Prerequisite>> checked) mutable {
            done(checked ? std::move(*checked) : std::vector<Prerequisite>{});
        });
}

Result<OpHandle> PrerequisiteService::start_remediate(PrerequisiteId id, DisconnectPolicy policy) {
    if (prerequisite_spec(id).remedy == Remedy::None)
        return std::unexpected(error_of(PrerequisiteErrorCode::NotRemediable, id));
    for (const Remediation& running : running_)
        if (running.id == id && !running.op->done()) return running.handle;
    // A cancelled remedy cannot be stopped, so the next one for its id runs after it.
    const bool busy = std::ranges::any_of(running_, [&](const Remediation& running) { return running.id == id; });

    auto [handle, op] = ops_.create<Prerequisite>(OpKind::Generic, policy, std::nullopt, RunnerMultiplier::Native,
                                                  std::chrono::milliseconds{kRemediateDeadline});
    running_.push_back(Remediation{.id = id, .handle = handle, .op = &op});
    op.progress(Progress{.phase = "remediating"});
    if (!busy) submit(running_.back());
    return handle;
}

void PrerequisiteService::submit(const Remediation& remediation) {
    Operation<Prerequisite>& op = *remediation.op;
    workers_.submit<RemediationPass>(
        [&probe = probe_, id = remediation.id](CancelToken cancel) -> Result<RemediationPass> {
            if (cancel.cancelled()) return RemediationPass{};
            return remediate(probe, id);
        },
        op.token(), strand_,
        [this, &op, id = remediation.id](Result<RemediationPass> pass) {
            std::erase_if(running_, [&](const Remediation& running) { return running.op == &op; });
            if (!pass) {
                op.complete(Failed{std::move(pass.error())});
            } else {
                if (pass->rechecked)
                    events_.publish(EventKind::PrerequisitesChanged, PrerequisitesChanged{std::move(*pass->rechecked)});
                if (pass->failure) op.complete(Failed{std::move(*pass->failure)});
                else if (pass->result) op.complete(Completed<Prerequisite>{std::move(*pass->result)});
                else op.complete(Cancelled{});
            }
            start_waiting(id);
        });
}

void PrerequisiteService::start_waiting(PrerequisiteId id) {
    for (auto it = running_.begin(); it != running_.end();) {
        if (it->id != id) {
            ++it;
        } else if (it->op->done()) {
            // Cancelled or timed out while it waited; completing lets the registry free it.
            it->op->complete(Cancelled{});
            it = running_.erase(it);
        } else {
            return submit(*it);
        }
    }
}

}  // namespace reboot::integration
