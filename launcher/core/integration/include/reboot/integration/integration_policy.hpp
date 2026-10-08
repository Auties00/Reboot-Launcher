#pragma once

#include <string_view>

#include "reboot/foundation/types.hpp"
#include "reboot/integration/entry_flavor.hpp"
#include "reboot/integration/entry_state.hpp"
#include "reboot/integration/integration_kind.hpp"

namespace reboot::ports {
struct IntegrationStatus;
}

namespace reboot::integration {

// What registrars put in IntegrationStatus::detail, instead of the command, for an inactive entry of ours.
inline constexpr std::string_view kDisabledDetail = "disabled";
inline constexpr std::string_view kRequiresApprovalDetail = "requires_approval";

// The registrar's verdict, refined: inactive is Disabled or AwaitingApproval, unexpected arguments Stale.
[[nodiscard]] EntryState classify(const ports::IntegrationStatus& found, EntryFlavor flavor);

// What an apply may leave behind: an opt-out the user set in the OS still counts as written.
[[nodiscard]] bool is_registered(EntryState state) noexcept;

enum class ReconcileAction : u8 { Leave, Write };

// Rewrites Stale and creates Absent entries, except declined kinds and Autostart, which is opt-in.
[[nodiscard]] ReconcileAction reconcile_action(IntegrationKind kind, EntryState state, bool declined) noexcept;

}  // namespace reboot::integration
