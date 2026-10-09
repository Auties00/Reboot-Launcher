#include "reboot/ux/onboarding.hpp"

#include <algorithm>
#include <array>
#include <utility>

#include "reboot/foundation/events.hpp"
#include "reboot/ux/guidance_state_store.hpp"
#include "messages.hpp"

namespace rb::ux {

namespace {

constexpr std::array<StepId, 9> kTourOrder{StepId::Welcome, StepId::Prerequisites, StepId::Profile,
                                           StepId::Library, StepId::Play,          StepId::Browser,
                                           StepId::HostListing, StepId::Backend,   StepId::Finish};

constexpr std::string_view kCoalesceKey = "onboarding";

struct StepText {
    MessageId title;
    MessageId body;
};

[[nodiscard]] StepText text_of(StepId step) {
    switch (step) {
        case StepId::Welcome: return {msg::kStepWelcomeTitle, msg::kStepWelcomeBody};
        case StepId::Prerequisites: return {msg::kStepPrerequisitesTitle, msg::kStepPrerequisitesBody};
        case StepId::Profile: return {msg::kStepProfileTitle, msg::kStepProfileBody};
        case StepId::Library: return {msg::kStepLibraryTitle, msg::kStepLibraryBody};
        case StepId::Play: return {msg::kStepPlayTitle, msg::kStepPlayBody};
        case StepId::Browser: return {msg::kStepBrowserTitle, msg::kStepBrowserBody};
        case StepId::HostListing: return {msg::kStepHostListingTitle, msg::kStepHostListingBody};
        case StepId::Backend: return {msg::kStepBackendTitle, msg::kStepBackendBody};
        case StepId::Finish: return {msg::kStepFinishTitle, msg::kStepFinishBody};
    }
    return {msg::kStepWelcomeTitle, msg::kStepWelcomeBody};
}

[[nodiscard]] bool applies(StepId step, const OnboardingContext& context) {
    if (step != StepId::Prerequisites) return true;
    return std::ranges::any_of(context.prerequisites, [](const ports::PrerequisiteStatus& p) { return !p.met; });
}

// The listing question is an explicit two-way choice; neither answer is preselected.
[[nodiscard]] std::vector<OnboardingChoice> choices_of(StepId step) {
    if (step != StepId::HostListing) return {};
    return {
        OnboardingChoice{OnboardingChoiceId::ListPublicly, msg::kChoiceListPublicly, SetDefaultHostListing{true}},
        OnboardingChoice{OnboardingChoiceId::KeepUnlisted, msg::kChoiceKeepUnlisted, SetDefaultHostListing{false}},
    };
}

[[nodiscard]] std::vector<SuggestedAction> actions_of(StepId step, const OnboardingContext& context) {
    std::vector<SuggestedAction> actions;
    switch (step) {
        case StepId::Prerequisites:
            for (const ports::PrerequisiteStatus& prerequisite : context.prerequisites)
                if (!prerequisite.met) actions.emplace_back(RemediatePrerequisite{prerequisite.id});
            break;
        case StepId::Profile:
            actions.emplace_back(EditDisplayName{contracts::backend::AccountRole::Client});
            break;
        case StepId::Library:
            if (context.library_empty) {
                actions.emplace_back(InstallBuild{});
                actions.emplace_back(ImportBuild{});
            }
            break;
        default:
            break;
    }
    return actions;
}

[[nodiscard]] const StepRecord* find_record(const OnboardingRecord& record, StepId step) {
    const auto it = std::ranges::find(record.steps, step, &StepRecord::step);
    return it == record.steps.end() ? nullptr : &*it;
}

// Records stay in tour order.
[[nodiscard]] StepRecord& record_for(OnboardingRecord& record, StepId step) {
    const auto rank = [](StepId id) { return std::ranges::find(kTourOrder, id) - kTourOrder.begin(); };
    auto it = std::ranges::find(record.steps, step, &StepRecord::step);
    if (it != record.steps.end()) return *it;
    it = std::ranges::find_if(record.steps, [&](const StepRecord& r) { return rank(r.step) > rank(step); });
    return *record.steps.insert(it, StepRecord{.step = step});
}

[[nodiscard]] bool finished(const OnboardingRecord& record, StepId step) {
    const StepRecord* r = find_record(record, step);
    return r != nullptr && (r->state == StepState::Done || r->state == StepState::Skipped);
}

// The first applicable step at or after `from` in tour order that is not Done or Skipped.
[[nodiscard]] std::optional<StepId> next_open(const OnboardingRecord& record, const OnboardingContext& context,
                                              std::size_t from) {
    for (std::size_t i = from; i < kTourOrder.size(); ++i)
        if (applies(kTourOrder[i], context) && !finished(record, kTourOrder[i])) return kTourOrder[i];
    return std::nullopt;
}

[[nodiscard]] std::size_t index_of(StepId step) {
    return static_cast<std::size_t>(std::ranges::find(kTourOrder, step) - kTourOrder.begin());
}

// The stored current step while it applies, else the next open one after it, e.g. once prerequisites are met.
[[nodiscard]] std::optional<StepId> effective_current(const OnboardingRecord& record, const OnboardingContext& context) {
    if (record.status != OnboardingStatus::InProgress) return std::nullopt;
    if (record.current && applies(*record.current, context)) return record.current;
    return next_open(record, context, record.current ? index_of(*record.current) : 0);
}

[[nodiscard]] OnboardingView make_view(const OnboardingRecord& record, const OnboardingContext& context) {
    OnboardingView view{.status = record.status, .current = effective_current(record, context)};
    for (const StepId id : kTourOrder) {
        if (!applies(id, context)) continue;
        const StepRecord* r = find_record(record, id);
        const StepText text = text_of(id);
        OnboardingStep step{
            .id = id,
            .title = {text.title, {}},
            .body = {text.body, {}},
            .choices = choices_of(id),
            .chosen = r != nullptr ? r->chosen : std::nullopt,
            .actions = actions_of(id, context),
        };
        if (view.current == id) step.state = StepState::Current;
        else if (r != nullptr && r->state != StepState::Current) step.state = r->state;
        view.steps.push_back(std::move(step));
    }
    return view;
}

[[nodiscard]] std::unexpected<Diagnostic> refuse(OnboardingErrorCode code, std::optional<StepId> step = std::nullopt,
                                                 std::optional<OnboardingChoiceId> choice = std::nullopt) {
    return std::unexpected(to_diagnostic(OnboardingError{code, step, choice}));
}

// Marks `step` done or skipped and moves to the next open step, completing the tour after the last.
void finish_step(OnboardingRecord& record, StepId step, StepState state, std::optional<OnboardingChoiceId> chosen,
                 const OnboardingContext& context) {
    StepRecord& r = record_for(record, step);
    r.state = state;
    r.chosen = chosen;
    record.current = next_open(record, context, index_of(step) + 1);
    if (!record.current) record.status = OnboardingStatus::Completed;
}

}  // namespace

Diagnostic to_diagnostic(const OnboardingError& error) {
    const std::string_view step = error.step ? persisted_name(*error.step) : std::string_view{};
    switch (error.code) {
        case OnboardingErrorCode::NotInProgress:
            return make_diag(ErrorDomain::Ux, msg::kOnboardingNotInProgress).kind(ErrorKind::Conflict);
        case OnboardingErrorCode::NotCurrentStep:
            return make_diag(ErrorDomain::Ux, msg::kOnboardingNotCurrentStep).arg("step", step).kind(ErrorKind::Conflict);
        case OnboardingErrorCode::ChoiceRequired:
            return make_diag(ErrorDomain::Ux, msg::kOnboardingChoiceRequired)
                .arg("step", step)
                .kind(ErrorKind::InvalidInput);
        case OnboardingErrorCode::ChoiceNotOffered:
            return make_diag(ErrorDomain::Ux, msg::kOnboardingChoiceNotOffered)
                .arg("step", step)
                .arg("choice", error.choice ? persisted_name(*error.choice) : std::string_view{})
                .kind(ErrorKind::InvalidInput);
    }
    return internal_bug("ux::to_diagnostic(OnboardingError)");
}

OnboardingView Onboarding::view(const OnboardingContext& context) const {
    return make_view(store_.current().onboarding, context);
}

Result<OnboardingView> Onboarding::start(const OnboardingContext& context) {
    OnboardingRecord next = store_.current().onboarding;
    std::optional<StepId> first = next_open(next, context, 0);
    if (next.status == OnboardingStatus::Completed || !first) {
        next.steps.clear();
        first = next_open(next, context, 0);
    }
    next.status = OnboardingStatus::InProgress;
    next.current = first;
    return commit(std::move(next), context);
}

Result<AdvanceResult> Onboarding::advance(StepId step, std::optional<OnboardingChoiceId> choice,
                                          const OnboardingContext& context) {
    OnboardingRecord next = store_.current().onboarding;
    if (next.status != OnboardingStatus::InProgress) return refuse(OnboardingErrorCode::NotInProgress, step);
    if (effective_current(next, context) != step) return refuse(OnboardingErrorCode::NotCurrentStep, step);

    std::optional<SuggestedAction> action;
    const std::vector<OnboardingChoice> choices = choices_of(step);
    if (!choices.empty() && !choice) return refuse(OnboardingErrorCode::ChoiceRequired, step);
    if (choice) {
        const auto offered = std::ranges::find(choices, *choice, &OnboardingChoice::id);
        if (offered == choices.end()) return refuse(OnboardingErrorCode::ChoiceNotOffered, step, choice);
        action = offered->action;
    }

    finish_step(next, step, StepState::Done, choice, context);
    Result<OnboardingView> view = commit(std::move(next), context);
    if (!view) return std::unexpected(std::move(view.error()));
    return AdvanceResult{std::move(*view), std::move(action)};
}

Result<OnboardingView> Onboarding::skip(StepId step, const OnboardingContext& context) {
    OnboardingRecord next = store_.current().onboarding;
    if (next.status != OnboardingStatus::InProgress) return refuse(OnboardingErrorCode::NotInProgress, step);
    if (effective_current(next, context) != step) return refuse(OnboardingErrorCode::NotCurrentStep, step);
    finish_step(next, step, StepState::Skipped, std::nullopt, context);
    return commit(std::move(next), context);
}

Result<OnboardingView> Onboarding::exit(const OnboardingContext& context) {
    OnboardingRecord next = store_.current().onboarding;
    if (next.status == OnboardingStatus::Completed) return refuse(OnboardingErrorCode::NotInProgress);
    if (next.status == OnboardingStatus::Exited) return make_view(next, context);
    next.status = OnboardingStatus::Exited;
    next.current.reset();
    return commit(std::move(next), context);
}

Result<OnboardingView> Onboarding::commit(OnboardingRecord record, const OnboardingContext& context) {
    // Only the current step is stored as Current, so a stale Current never outlives a move.
    for (StepRecord& r : record.steps)
        if (r.state == StepState::Current) r.state = StepState::Pending;
    if (record.current) record_for(record, *record.current).state = StepState::Current;

    GuidanceState state = store_.current();
    state.onboarding = std::move(record);
    if (Result<void> written = store_.replace(std::move(state)); !written) return std::unexpected(std::move(written.error()));

    OnboardingView view = make_view(store_.current().onboarding, context);
    events_.publish(EventKind::OnboardingChanged, OnboardingChangedEvent{view},
                    EventScope{.coalesce_key = std::string(kCoalesceKey)});
    return view;
}

}  // namespace rb::ux
