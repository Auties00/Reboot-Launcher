#include <algorithm>
#include <optional>
#include <variant>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "reboot/foundation/events.hpp"
#include "reboot/testing/event_recorder.hpp"
#include "reboot/ux/onboarding.hpp"
#include "test_support.hpp"

using namespace rb;
using namespace rb::ux;

namespace {

struct Fixture {
    test::MemoryGuidanceStore store;
    EventBus bus{EngineEpoch{1}};
    testing::EventRecorder recorder{bus};
    Onboarding onboarding{store, bus};
    OnboardingContext context;

    [[nodiscard]] std::size_t changes() {
        recorder.pump();
        return recorder.count(EventKind::OnboardingChanged);
    }

    // Advances through every step up to `target`, answering the listing question with KeepUnlisted.
    void advance_to(StepId target) {
        while (true) {
            const OnboardingView view = onboarding.view(context);
            REQUIRE(view.current);
            if (*view.current == target) return;
            const std::optional<OnboardingChoiceId> choice =
                *view.current == StepId::HostListing ? std::optional(OnboardingChoiceId::KeepUnlisted) : std::nullopt;
            REQUIRE(onboarding.advance(*view.current, choice, context));
        }
    }
};

[[nodiscard]] std::vector<StepId> ids(const OnboardingView& view) {
    std::vector<StepId> out;
    for (const OnboardingStep& step : view.steps) out.push_back(step.id);
    return out;
}

[[nodiscard]] const OnboardingStep& step_of(const OnboardingView& view, StepId id) {
    const auto it = std::ranges::find(view.steps, id, &OnboardingStep::id);
    REQUIRE(it != view.steps.end());
    return *it;
}

const std::vector<StepId> kAllButPrerequisites{StepId::Welcome, StepId::Profile,     StepId::Library,
                                               StepId::Play,    StepId::Browser,     StepId::HostListing,
                                               StepId::Backend, StepId::Finish};

}  // namespace

TEST_CASE("persisted names of steps and choices round-trip") {
    for (const StepId step : {StepId::Welcome, StepId::Prerequisites, StepId::Profile, StepId::Library, StepId::Play,
                              StepId::Browser, StepId::HostListing, StepId::Backend, StepId::Finish})
        CHECK(parse_step_id(persisted_name(step)) == step);
    CHECK(persisted_name(StepId::HostListing) == "host_listing");
    CHECK_FALSE(parse_step_id("Welcome"));
    CHECK(persisted_name(OnboardingChoiceId::ListPublicly) == "list_publicly");
    CHECK(parse_choice_id("keep_unlisted") == OnboardingChoiceId::KeepUnlisted);
    CHECK_FALSE(parse_choice_id("private"));
}

TEST_CASE("a fresh tour is offered with every applicable step pending") {
    Fixture f;
    const OnboardingView view = f.onboarding.view(f.context);
    CHECK(view.status == OnboardingStatus::Offered);
    CHECK_FALSE(view.current);
    CHECK(ids(view) == kAllButPrerequisites);
    for (const OnboardingStep& step : view.steps) CHECK(step.state == StepState::Pending);
    CHECK(step_of(view, StepId::Welcome).title.id.id == "ux.step_welcome_title");
    CHECK(step_of(view, StepId::Finish).body.id.id == "ux.step_finish_body");
}

TEST_CASE("steps and actions follow the live context") {
    Fixture f;
    f.context.prerequisites = {{"wine", false, MessageId{"platform.install_wine"}}, {"rosetta", true, std::nullopt}};
    f.context.library_empty = true;
    const OnboardingView view = f.onboarding.view(f.context);
    REQUIRE(view.steps.size() == 9);
    CHECK(view.steps[1].id == StepId::Prerequisites);

    const OnboardingStep& prerequisites = step_of(view, StepId::Prerequisites);
    REQUIRE(prerequisites.actions.size() == 1);
    CHECK(std::get<RemediatePrerequisite>(prerequisites.actions[0]).prerequisite_id == "wine");

    const OnboardingStep& library = step_of(view, StepId::Library);
    REQUIRE(library.actions.size() == 2);
    CHECK(std::holds_alternative<InstallBuild>(library.actions[0]));
    CHECK(std::holds_alternative<ImportBuild>(library.actions[1]));

    const OnboardingStep& profile = step_of(view, StepId::Profile);
    REQUIRE(profile.actions.size() == 1);
    CHECK(std::get<EditDisplayName>(profile.actions[0]).role == contracts::backend::AccountRole::Client);

    const OnboardingStep& listing = step_of(view, StepId::HostListing);
    REQUIRE(listing.choices.size() == 2);
    CHECK(listing.choices[0].id == OnboardingChoiceId::ListPublicly);
    CHECK(std::get<SetDefaultHostListing>(*listing.choices[0].action).listed);
    CHECK_FALSE(std::get<SetDefaultHostListing>(*listing.choices[1].action).listed);
    CHECK(listing.choices[1].label.id == "ux.choice_keep_unlisted");

    f.context.library_empty = false;
    CHECK(step_of(f.onboarding.view(f.context), StepId::Library).actions.empty());
}

TEST_CASE("start, advance and skip walk the tour to completion") {
    Fixture f;
    const Result<OnboardingView> started = f.onboarding.start(f.context);
    REQUIRE(started);
    CHECK(started->status == OnboardingStatus::InProgress);
    CHECK(started->current == StepId::Welcome);
    CHECK(step_of(*started, StepId::Welcome).state == StepState::Current);
    CHECK(f.store.current().onboarding.current == StepId::Welcome);

    const Result<AdvanceResult> welcome = f.onboarding.advance(StepId::Welcome, std::nullopt, f.context);
    REQUIRE(welcome);
    CHECK_FALSE(welcome->action);
    CHECK(welcome->view.current == StepId::Profile);
    CHECK(step_of(welcome->view, StepId::Welcome).state == StepState::Done);

    const Result<OnboardingView> skipped = f.onboarding.skip(StepId::Profile, f.context);
    REQUIRE(skipped);
    CHECK(skipped->current == StepId::Library);
    CHECK(step_of(*skipped, StepId::Profile).state == StepState::Skipped);

    f.advance_to(StepId::HostListing);
    const Result<AdvanceResult> listing =
        f.onboarding.advance(StepId::HostListing, OnboardingChoiceId::ListPublicly, f.context);
    REQUIRE(listing);
    REQUIRE(listing->action);
    CHECK(std::get<SetDefaultHostListing>(*listing->action).listed);
    CHECK(step_of(listing->view, StepId::HostListing).chosen == OnboardingChoiceId::ListPublicly);

    f.advance_to(StepId::Finish);
    const Result<AdvanceResult> finish = f.onboarding.advance(StepId::Finish, std::nullopt, f.context);
    REQUIRE(finish);
    CHECK(finish->view.status == OnboardingStatus::Completed);
    CHECK_FALSE(finish->view.current);
    CHECK(f.store.current().onboarding.status == OnboardingStatus::Completed);
    for (const StepRecord& record : f.store.current().onboarding.steps) CHECK(record.state != StepState::Current);
}

TEST_CASE("skipping the last step completes the tour") {
    Fixture f;
    REQUIRE(f.onboarding.start(f.context));
    f.advance_to(StepId::Finish);
    const Result<OnboardingView> skipped = f.onboarding.skip(StepId::Finish, f.context);
    REQUIRE(skipped);
    CHECK(skipped->status == OnboardingStatus::Completed);
}

TEST_CASE("every change is written through and published") {
    Fixture f;
    REQUIRE(f.onboarding.start(f.context));
    CHECK(f.changes() == 1);
    REQUIRE(f.onboarding.advance(StepId::Welcome, std::nullopt, f.context));
    CHECK(f.changes() == 2);
    REQUIRE(f.onboarding.skip(StepId::Profile, f.context));
    CHECK(f.changes() == 3);
    REQUIRE(f.onboarding.exit(f.context));
    CHECK(f.changes() == 4);
    CHECK(f.store.writes == 4);
    const auto payloads = f.recorder.payloads<OnboardingChangedEvent>(EventKind::OnboardingChanged);
    CHECK(payloads.back()->view.status == OnboardingStatus::Exited);
    CHECK(f.recorder.events().back().coalesce_key == "onboarding");
}

TEST_CASE("a slow reader sees only the latest onboarding view") {
    Fixture f;
    REQUIRE(f.onboarding.start(f.context));
    REQUIRE(f.onboarding.advance(StepId::Welcome, std::nullopt, f.context));
    REQUIRE(f.onboarding.skip(StepId::Profile, f.context));
    CHECK(f.changes() == 1);
    const auto payloads = f.recorder.payloads<OnboardingChangedEvent>(EventKind::OnboardingChanged);
    CHECK(payloads.back()->view.current == StepId::Library);
}

TEST_CASE("a stale or double advance is refused") {
    Fixture f;
    REQUIRE(f.onboarding.start(f.context));
    REQUIRE(f.onboarding.advance(StepId::Welcome, std::nullopt, f.context));
    const std::size_t published = f.changes();

    const Result<AdvanceResult> again = f.onboarding.advance(StepId::Welcome, std::nullopt, f.context);
    REQUIRE_FALSE(again);
    CHECK(again.error().id == "ux.onboarding_not_current_step");
    CHECK(again.error().kind == ErrorKind::Conflict);
    CHECK(std::get<std::string>(*again.error().find_arg("step")) == "welcome");

    const Result<OnboardingView> ahead = f.onboarding.skip(StepId::Play, f.context);
    REQUIRE_FALSE(ahead);
    CHECK(ahead.error().id == "ux.onboarding_not_current_step");
    CHECK(f.onboarding.view(f.context).current == StepId::Profile);
    CHECK(f.changes() == published);
    CHECK(f.store.writes == 2);
}

TEST_CASE("choices are validated against the step") {
    Fixture f;
    REQUIRE(f.onboarding.start(f.context));

    const Result<AdvanceResult> unasked =
        f.onboarding.advance(StepId::Welcome, OnboardingChoiceId::ListPublicly, f.context);
    REQUIRE_FALSE(unasked);
    CHECK(unasked.error().id == "ux.onboarding_choice_not_offered");
    CHECK(std::get<std::string>(*unasked.error().find_arg("choice")) == "list_publicly");

    f.advance_to(StepId::HostListing);
    const Result<AdvanceResult> missing = f.onboarding.advance(StepId::HostListing, std::nullopt, f.context);
    REQUIRE_FALSE(missing);
    CHECK(missing.error().id == "ux.onboarding_choice_required");
    CHECK(missing.error().kind == ErrorKind::InvalidInput);
    CHECK(std::get<std::string>(*missing.error().find_arg("step")) == "host_listing");

    const Result<OnboardingView> skipped = f.onboarding.skip(StepId::HostListing, f.context);
    REQUIRE(skipped);
    CHECK_FALSE(step_of(*skipped, StepId::HostListing).chosen);
}

TEST_CASE("nothing but start and exit works outside a running tour") {
    Fixture f;
    const Result<AdvanceResult> advance = f.onboarding.advance(StepId::Welcome, std::nullopt, f.context);
    REQUIRE_FALSE(advance);
    CHECK(advance.error().id == "ux.onboarding_not_in_progress");
    CHECK_FALSE(f.onboarding.skip(StepId::Welcome, f.context));
    CHECK(f.store.writes == 0);
    CHECK(f.changes() == 0);
}

TEST_CASE("exit declines the offer, keeps progress, and start resumes from it") {
    Fixture f;
    const Result<OnboardingView> declined = f.onboarding.exit(f.context);
    REQUIRE(declined);
    CHECK(declined->status == OnboardingStatus::Exited);

    const Result<OnboardingView> again = f.onboarding.exit(f.context);
    REQUIRE(again);
    CHECK(f.store.writes == 1);

    REQUIRE(f.onboarding.start(f.context));
    f.advance_to(StepId::Play);
    REQUIRE(f.onboarding.exit(f.context));
    const OnboardingView exited = f.onboarding.view(f.context);
    CHECK_FALSE(exited.current);
    CHECK(step_of(exited, StepId::Library).state == StepState::Done);
    CHECK(step_of(exited, StepId::Play).state == StepState::Pending);

    const Result<OnboardingView> resumed = f.onboarding.start(f.context);
    REQUIRE(resumed);
    CHECK(resumed->current == StepId::Play);
    CHECK(step_of(*resumed, StepId::Welcome).state == StepState::Done);
}

TEST_CASE("start restarts a completed tour from the first step") {
    Fixture f;
    REQUIRE(f.onboarding.start(f.context));
    f.advance_to(StepId::Finish);
    REQUIRE(f.onboarding.advance(StepId::Finish, std::nullopt, f.context));
    REQUIRE_FALSE(f.onboarding.exit(f.context));

    const Result<OnboardingView> restarted = f.onboarding.start(f.context);
    REQUIRE(restarted);
    CHECK(restarted->status == OnboardingStatus::InProgress);
    CHECK(restarted->current == StepId::Welcome);
    for (const OnboardingStep& step : restarted->steps)
        CHECK(step.state == (step.id == StepId::Welcome ? StepState::Current : StepState::Pending));
    CHECK_FALSE(step_of(*restarted, StepId::HostListing).chosen);
}

TEST_CASE("a step that stops applying hands over to the next open step") {
    Fixture f;
    f.context.prerequisites = {{"wine", false, MessageId{"platform.install_wine"}}};
    REQUIRE(f.onboarding.start(f.context));
    REQUIRE(f.onboarding.advance(StepId::Welcome, std::nullopt, f.context));
    CHECK(f.onboarding.view(f.context).current == StepId::Prerequisites);

    f.context.prerequisites[0].met = true;
    const OnboardingView view = f.onboarding.view(f.context);
    CHECK(view.current == StepId::Profile);
    CHECK(ids(view) == kAllButPrerequisites);
    CHECK_FALSE(f.onboarding.advance(StepId::Prerequisites, std::nullopt, f.context));
    REQUIRE(f.onboarding.advance(StepId::Profile, std::nullopt, f.context));
    CHECK(f.onboarding.view(f.context).current == StepId::Library);
}

TEST_CASE("a step that starts applying is visited when the tour resumes") {
    Fixture f;
    REQUIRE(f.onboarding.start(f.context));
    f.advance_to(StepId::Play);
    REQUIRE(f.onboarding.exit(f.context));

    f.context.prerequisites = {{"wine", false, MessageId{"platform.install_wine"}}};
    const Result<OnboardingView> resumed = f.onboarding.start(f.context);
    REQUIRE(resumed);
    CHECK(resumed->current == StepId::Prerequisites);
    const Result<AdvanceResult> fixed = f.onboarding.advance(StepId::Prerequisites, std::nullopt, f.context);
    REQUIRE(fixed);
    CHECK(fixed->view.current == StepId::Play);
}

TEST_CASE("a failed write changes nothing and publishes nothing") {
    Fixture f;
    REQUIRE(f.onboarding.start(f.context));
    const std::size_t before = f.changes();

    f.store.fail_next = true;
    const Result<AdvanceResult> failed = f.onboarding.advance(StepId::Welcome, std::nullopt, f.context);
    REQUIRE_FALSE(failed);
    CHECK(failed.error().id == "storage.write_failed");
    CHECK(f.onboarding.view(f.context).current == StepId::Welcome);
    CHECK(f.changes() == before);

    f.store.fail_next = true;
    CHECK_FALSE(f.onboarding.exit(f.context));
    CHECK(f.onboarding.view(f.context).status == OnboardingStatus::InProgress);

    REQUIRE(f.onboarding.advance(StepId::Welcome, std::nullopt, f.context));
    CHECK(f.changes() == before + 1);
}

TEST_CASE("a tour abandoned by its UI resumes where it stopped") {
    Fixture f;
    REQUIRE(f.onboarding.start(f.context));
    f.advance_to(StepId::Browser);

    Onboarding reopened(f.store, f.bus);
    const OnboardingView view = reopened.view(f.context);
    CHECK(view.status == OnboardingStatus::InProgress);
    CHECK(view.current == StepId::Browser);
}

TEST_CASE("OnboardingError converts to its diagnostic") {
    const Diagnostic not_running = to_diagnostic(OnboardingError{OnboardingErrorCode::NotInProgress});
    CHECK(not_running.id == "ux.onboarding_not_in_progress");
    CHECK(not_running.domain == ErrorDomain::Ux);
    CHECK(not_running.args.empty());
}
