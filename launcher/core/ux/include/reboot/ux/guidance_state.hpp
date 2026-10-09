#pragma once

#include <chrono>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "reboot/foundation/diag.hpp"
#include "reboot/foundation/types.hpp"
#include "reboot/ux/notice.hpp"
#include "reboot/ux/onboarding_step.hpp"

namespace rb::ux {

// Offered shows the first-run banner; Exited and Completed both stop it, and start() reopens the tour.
enum class OnboardingStatus : u8 { Offered, InProgress, Completed, Exited };

struct StepRecord {
    StepId step{};
    StepState state = StepState::Pending;
    std::optional<OnboardingChoiceId> chosen;
};

// Progress is saved per step, so a tour that a UI abandons resumes where it stopped.
struct OnboardingRecord {
    OnboardingStatus status = OnboardingStatus::Offered;
    std::optional<StepId> current;
    std::vector<StepRecord> steps;
};

// The dismissed record keeps a one-time notice from returning.
struct OneTimeNoticeRecord {
    NoticeKey key;
    std::chrono::system_clock::time_point created_at;
    std::vector<std::pair<std::string, Arg>> args;
    bool dismissed = false;
};

// Capabilities: onboarding-ux-flows.first-run-tour, onboarding-ux-flows.+53.
// Persisted whole as StateDocument's "guidance" member; onboarding.completed is derived from status.
struct GuidanceState {
    OnboardingRecord onboarding;
    std::vector<OneTimeNoticeRecord> notices;
};

}  // namespace rb::ux
