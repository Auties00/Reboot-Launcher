#pragma once

#include <optional>
#include <vector>

#include "reboot/foundation/diag.hpp"
#include "reboot/foundation/types.hpp"
#include "reboot/ports/os_services.hpp"
#include "reboot/ux/guidance_state.hpp"
#include "reboot/ux/onboarding_step.hpp"
#include "reboot/ux/suggested_action.hpp"

namespace reboot {
class EventBus;
}

namespace reboot::ux {

class IGuidanceStateStore;

// Live facts that decide which steps apply and what they offer; read fresh on every call.
struct OnboardingContext {
    // Prerequisites appears only while one of these is unmet.
    std::vector<ports::PrerequisiteStatus> prerequisites;
    // Library offers InstallBuild and ImportBuild while true.
    bool library_empty = false;
};

struct OnboardingView {
    OnboardingStatus status = OnboardingStatus::Offered;
    std::optional<StepId> current;
    // Applicable steps in tour order.
    std::vector<OnboardingStep> steps;
};

// EventKind::OnboardingChanged, coalesced, after every start, advance, skip and exit.
struct OnboardingChangedEvent {
    OnboardingView view;
};

struct AdvanceResult {
    OnboardingView view;
    // The chosen choice's action, for the UI to perform.
    std::optional<SuggestedAction> action;
};

enum class OnboardingErrorCode : u8 { NotInProgress, NotCurrentStep, ChoiceRequired, ChoiceNotOffered };

struct OnboardingError {
    OnboardingErrorCode code{};
    std::optional<StepId> step;
    std::optional<OnboardingChoiceId> choice;
};

// The step and choice args are persisted names.
[[nodiscard]] Diagnostic to_diagnostic(const OnboardingError& error);

// Capabilities: onboarding-ux-flows.first-run-tour, onboarding-ux-flows.+53, discoverable-default.
// Strand-only. HostListing asks List publicly / Keep unlisted and never changes the listing itself.
class Onboarding {
public:
    Onboarding(IGuidanceStateStore& store, EventBus& events) : store_(store), events_(events) {}

    [[nodiscard]] OnboardingView view(const OnboardingContext& context) const;

    // From the first applicable step; also restarts a Completed or Exited tour.
    Result<OnboardingView> start(const OnboardingContext& context);

    // `step` must be current, so a stale double-click cannot skip ahead.
    Result<AdvanceResult> advance(StepId step, std::optional<OnboardingChoiceId> choice,
                                  const OnboardingContext& context);
    Result<OnboardingView> skip(StepId step, const OnboardingContext& context);

    // Leaves the tour, or declines the first-run offer; progress is kept for start().
    Result<OnboardingView> exit(const OnboardingContext& context);

private:
    IGuidanceStateStore& store_;
    EventBus& events_;
};

}  // namespace reboot::ux
