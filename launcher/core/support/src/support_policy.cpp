#include "reboot/support/support_policy.hpp"

#include <algorithm>

#include "reboot/support/version_cap.hpp"

namespace reboot::support {
namespace {

void add_reason(std::vector<SupportReason>& reasons, SupportReason reason) {
    if (std::ranges::find(reasons, reason) == reasons.end()) reasons.push_back(reason);
}

// Puts the Blocked reasons first and returns the worst tier.
[[nodiscard]] SupportTier settle(std::vector<SupportReason>& reasons) {
    std::ranges::stable_partition(reasons,
                                  [](SupportReason reason) { return reason_tier(reason) == SupportTier::Blocked; });
    SupportTier tier = SupportTier::Tested;
    for (const SupportReason reason : reasons) tier = worst(tier, reason_tier(reason));
    return tier;
}

void add_key(std::vector<SupportCellKey>& keys, const SupportCellKey& key) {
    if (std::ranges::find(keys, key) == keys.end()) keys.push_back(key);
}

[[nodiscard]] bool covered_by(const HostInputs& server, const GameVersion& version, std::optional<Changelist> cl) {
    return std::ranges::any_of(server.ranges, [&](const VersionRange& range) { return range.contains(version, cl); });
}

}  // namespace

SupportCell SupportPolicy::rate_cell(const SupportCellKey& key, const std::optional<HostInputs>& server) const {
    SupportCell cell{.key = key,
                     .tier = SupportTier::Untested,
                     .provider = SupportProvider::Ours,
                     .reasons = {},
                     .evidence = std::nullopt};
    if (above_version_cap(key.range.min)) add_reason(cell.reasons, SupportReason::AboveVersionCap);

    std::optional<CellInputs> current;
    if (key.role == SupportRole::Play) {
        const auto pin = std::ranges::find(inputs_.runners, key.runner, &RunnerPin::runner);
        if (pin == inputs_.runners.end())
            add_reason(cell.reasons, SupportReason::RunnerUnavailable);
        else
            current = PlayCellInputs{.client_dll_sha256 = inputs_.client_dll_sha256,
                                     .backend_content = inputs_.backend_content,
                                     .runner_pin = pin->runtime_id};
    } else if (key.runner != ports::RunnerKind::Native) {
        add_reason(cell.reasons, SupportReason::RunnerUnavailable);
    } else if (!server) {
        add_reason(cell.reasons, SupportReason::GameServerUnavailable);
    } else {
        current = server->pin;
    }

    if (current) {
        bool any_record = false;
        const EvidenceRecord* newest = nullptr;
        for (const EvidenceRecord& record : inputs_.evidence) {
            if (record.os != inputs_.os || record.cell != key) continue;
            any_record = true;
            if (record.inputs != *current) continue;
            // On a tie the failed run wins, so a pass never hides a failure logged at the same time.
            if (newest == nullptr || record.recorded_at > newest->recorded_at ||
                (record.recorded_at == newest->recorded_at && record.result == EvidenceResult::Fail))
                newest = &record;
        }
        if (!any_record)
            add_reason(cell.reasons, SupportReason::NoEvidence);
        else if (newest == nullptr)
            add_reason(cell.reasons, SupportReason::EvidenceStale);
        else if (newest->result == EvidenceResult::Fail)
            add_reason(cell.reasons, SupportReason::EvidenceFailed);
        else
            cell.evidence = *newest;
    }
    cell.tier = settle(cell.reasons);
    return cell;
}

SupportVerdict SupportPolicy::evaluate(const SupportQuery& query) const {
    SupportVerdict verdict;
    const bool host = query.role == SupportRole::Host;

    if (!query.version) {
        add_reason(verdict.reasons, SupportReason::VersionUnknown);
    } else if (above_version_cap(*query.version)) {
        if (query.imported && query.above_cap_opt_in) {
            add_reason(verdict.reasons, SupportReason::AboveVersionCapOptedIn);
        } else {
            add_reason(verdict.reasons, SupportReason::AboveVersionCap);
            verdict.opt_in_available = query.imported;
        }
    }

    if (host) {
        if (query.runner != ports::RunnerKind::Native) add_reason(verdict.reasons, SupportReason::RunnerUnavailable);
        if (!query.server)
            add_reason(verdict.reasons, SupportReason::GameServerUnavailable);
        else if (query.version && !covered_by(*query.server, *query.version, query.cl))
            add_reason(verdict.reasons, SupportReason::NotCoveredByGameServer);
    } else {
        if (std::ranges::find(inputs_.runners, query.runner, &RunnerPin::runner) == inputs_.runners.end())
            add_reason(verdict.reasons, SupportReason::RunnerUnavailable);
        if (query.custom_auth_dll) {
            verdict.provider = SupportProvider::Custom;
            add_reason(verdict.reasons, SupportReason::CustomAuthDll);
        }
        if (!query.embedded_backend) add_reason(verdict.reasons, SupportReason::ExternalBackend);
    }

    if (query.version) {
        std::vector<SupportCellKey> keys;
        for (const EvidenceRecord& record : inputs_.evidence)
            if (record.os == inputs_.os && record.cell.role == query.role && record.cell.runner == query.runner &&
                record.cell.range.contains(*query.version, query.cl))
                add_key(keys, record.cell);
        if (host && query.server)
            for (const VersionRange& range : query.server->ranges)
                if (range.contains(*query.version, query.cl))
                    add_key(keys, {.range = range, .role = SupportRole::Host, .runner = ports::RunnerKind::Native});

        for (const SupportCellKey& key : keys) {
            SupportCell cell = rate_cell(key, query.server);
            if (!verdict.cell || cell.tier < verdict.cell->tier) verdict.cell = std::move(cell);
        }
        if (verdict.cell)
            for (const SupportReason reason : verdict.cell->reasons) add_reason(verdict.reasons, reason);
        else
            add_reason(verdict.reasons, SupportReason::NoEvidence);
    }

    verdict.tier = settle(verdict.reasons);
    return verdict;
}

AutoServerVerdict SupportPolicy::evaluate_with_auto_server(const SupportQuery& play) const {
    SupportQuery host = play;
    host.role = SupportRole::Host;
    host.runner = ports::RunnerKind::Native;
    host.custom_auth_dll = false;
    host.embedded_backend = true;

    AutoServerVerdict verdict{.play = evaluate(play), .host = evaluate(host), .tier = SupportTier::Untested};
    verdict.tier = worst(verdict.play.tier, verdict.host.tier);
    return verdict;
}

std::vector<SupportCell> SupportPolicy::cells(const std::optional<HostInputs>& server) const {
    std::vector<SupportCellKey> keys;
    for (const EvidenceRecord& record : inputs_.evidence)
        if (record.os == inputs_.os) add_key(keys, record.cell);
    if (server)
        for (const VersionRange& range : server->ranges)
            add_key(keys, {.range = range, .role = SupportRole::Host, .runner = ports::RunnerKind::Native});

    std::vector<SupportCell> out;
    out.reserve(keys.size());
    for (const SupportCellKey& key : keys) out.push_back(rate_cell(key, server));
    return out;
}

}  // namespace reboot::support
