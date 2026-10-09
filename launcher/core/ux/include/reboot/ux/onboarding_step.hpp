#pragma once

#include <optional>
#include <string_view>
#include <vector>

#include "reboot/foundation/diag.hpp"
#include "reboot/foundation/types.hpp"
#include "reboot/ux/message_text.hpp"
#include "reboot/ux/suggested_action.hpp"

namespace rb::ux {

// Persisted by name; reordering is free, renaming is a migration.
enum class StepId : u8 { Welcome, Prerequisites, Profile, Library, Play, Browser, HostListing, Backend, Finish };

enum class StepState : u8 { Pending, Current, Done, Skipped };

// Persisted by name, like StepId.
enum class OnboardingChoiceId : u8 { ListPublicly, KeepUnlisted };

[[nodiscard]] std::string_view persisted_name(StepId step);
[[nodiscard]] std::optional<StepId> parse_step_id(std::string_view name);
[[nodiscard]] std::string_view persisted_name(OnboardingChoiceId choice);
[[nodiscard]] std::optional<OnboardingChoiceId> parse_choice_id(std::string_view name);

struct OnboardingChoice {
    OnboardingChoiceId id{};
    MessageId label;
    // Returned by Onboarding::advance for the UI to perform.
    std::optional<SuggestedAction> action;
};

// Capabilities: onboarding-ux-flows.first-run-tour, onboarding-ux-flows.+53.
// UIs anchor a step to their own control by `id`; a missing anchor renders as a plain card.
struct OnboardingStep {
    StepId id{};
    StepState state = StepState::Pending;
    MessageText title;
    MessageText body;
    // Non-empty: advancing needs one of these; skipping does not.
    std::vector<OnboardingChoice> choices;
    std::optional<OnboardingChoiceId> chosen;
    // Offered alongside the step, e.g. Install a build when the library is empty.
    std::vector<SuggestedAction> actions;
};

}  // namespace rb::ux
