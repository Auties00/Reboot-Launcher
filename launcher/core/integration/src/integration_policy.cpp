#include "reboot/integration/integration_policy.hpp"

#include <algorithm>
#include <optional>
#include <string>
#include <vector>

#include "reboot/integration/entry_command.hpp"
#include "reboot/ports/os_services.hpp"

namespace reboot::integration {

namespace {

// A suffix, since a registrar may put a wrapper such as env before the program.
[[nodiscard]] bool ends_with_args(const std::vector<std::string>& tokens, const std::vector<std::string>& args) {
    if (tokens.size() <= args.size()) return false;
    return std::equal(args.rbegin(), args.rend(), tokens.rbegin());
}

}  // namespace

EntryState classify(const ports::IntegrationStatus& found, EntryFlavor flavor) {
    switch (found.state) {
        case ports::IntegrationState::Absent: return EntryState::Absent;
        case ports::IntegrationState::Foreign: return EntryState::Foreign;
        case ports::IntegrationState::Stale: return EntryState::Stale;
        case ports::IntegrationState::Ours: break;
    }
    if (found.detail == kDisabledDetail) return EntryState::Disabled;
    if (found.detail == kRequiresApprovalDetail) return EntryState::AwaitingApproval;
    const std::optional<std::vector<std::string>> args = expected_args(found.kind, flavor);
    if (!args) return EntryState::Ours;
    const std::optional<std::vector<std::string>> tokens = split_entry_command(found.detail);
    if (!tokens || !ends_with_args(*tokens, *args)) return EntryState::Stale;
    return EntryState::Ours;
}

bool is_registered(EntryState state) noexcept {
    return state == EntryState::Ours || state == EntryState::Disabled || state == EntryState::AwaitingApproval;
}

ReconcileAction reconcile_action(IntegrationKind kind, EntryState state, bool declined) noexcept {
    if (declined) return ReconcileAction::Leave;
    if (state == EntryState::Stale) return ReconcileAction::Write;
    if (state == EntryState::Absent && kind != IntegrationKind::Autostart) return ReconcileAction::Write;
    return ReconcileAction::Leave;
}

}  // namespace reboot::integration
