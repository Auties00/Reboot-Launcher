#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <chrono>
#include <future>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

#include "integration_test_support.hpp"
#include "reboot/foundation/executor.hpp"
#include "reboot/integration/prerequisite_service.hpp"
#include "reboot/integration/prerequisite_spec.hpp"
#include "reboot/integration/prerequisites_changed.hpp"
#include "reboot/ports/os_services.hpp"
#include "reboot/testing/event_recorder.hpp"
#include "reboot/testing/fake_prereqs.hpp"

using namespace reboot;
using namespace reboot::integration;

namespace {

// A probe whose remedy succeeds without changing anything, and that can hold its worker.
class StubbornProbe final : public ports::IPrerequisiteProbe {
public:
    explicit StubbornProbe(std::vector<ports::PrerequisiteStatus> statuses) : statuses_(std::move(statuses)) {}

    std::vector<ports::PrerequisiteStatus> check() override {
        const std::scoped_lock lock(mutex_);
        return statuses_;
    }
    Result<void> remediate(std::string_view) override {
        if (gate_) {
            if (!entered_set_.exchange(true)) entered_.set_value();
            gate_->wait();
        }
        const std::scoped_lock lock(mutex_);
        ++remedies_;
        return {};
    }

    // Holds the first remedy until `gate` opens; entered() is ready once it is held.
    void hold(std::shared_future<void> gate) { gate_ = std::move(gate); }
    [[nodiscard]] std::future<void> entered() { return entered_.get_future(); }
    [[nodiscard]] int remedies() const {
        const std::scoped_lock lock(mutex_);
        return remedies_;
    }

private:
    mutable std::mutex mutex_;
    std::vector<ports::PrerequisiteStatus> statuses_;
    std::optional<std::shared_future<void>> gate_;
    std::promise<void> entered_;
    std::atomic<bool> entered_set_{false};
    int remedies_ = 0;
};

template <class Probe>
struct Fixture {
    explicit Fixture(Probe& probe) : service(probe, workers, rig.strand, rig.ops, rig.events) {}

    [[nodiscard]] std::vector<Prerequisite> check() {
        std::optional<std::vector<Prerequisite>> checked;
        service.check({}, [&](std::vector<Prerequisite> result) { checked = std::move(result); });
        rig.strand.run_until([&] { return checked.has_value(); });
        return *checked;
    }

    [[nodiscard]] std::vector<PrerequisitesChanged> changes() {
        recorder.pump();
        std::vector<PrerequisitesChanged> out;
        for (const auto* change : recorder.payloads<PrerequisitesChanged>(EventKind::PrerequisitesChanged))
            out.push_back(*change);
        recorder.clear();
        return out;
    }

    test::OpRig rig;
    WorkerPool workers{2};
    testing::EventRecorder recorder{rig.events, EventFilter{.kinds = {EventKind::PrerequisitesChanged}}};
    PrerequisiteService service;
};

[[nodiscard]] ports::PrerequisiteStatus status(std::string id, bool met,
                                               std::optional<MessageId> hint = std::nullopt) {
    return ports::PrerequisiteStatus{std::move(id), met, hint};
}

}  // namespace

TEST_CASE("check keeps known ids in order, with their spec, and drops the rest", "[integration][prerequisites]") {
    testing::FakePrereqs probe;
    probe.set({status("linux.vulkan", true), status("future.thing", false), status("linux.python3", false,
                                                                                    MessageId{"platform.install_python"}),
               status("linux.vulkan", false)});
    Fixture f(probe);
    const std::vector<Prerequisite> checked = f.check();
    REQUIRE(checked.size() == 2);
    CHECK(checked[0].id == PrerequisiteId::LinuxPython3);
    CHECK_FALSE(checked[0].met);
    CHECK(checked[0].impact == PrerequisiteImpact::BlocksPlay);
    CHECK(checked[0].remedy == Remedy::None);
    CHECK(checked[0].guidance == prerequisite_spec(PrerequisiteId::LinuxPython3).guidance);
    CHECK(checked[0].platform_hint == MessageId{"platform.install_python"});
    CHECK(checked[1].id == PrerequisiteId::LinuxVulkan);
    CHECK(checked[1].met);
    CHECK(f.changes().empty());
}

TEST_CASE("a prerequisite with no remedy is refused without an op", "[integration][prerequisites]") {
    testing::FakePrereqs probe;
    Fixture f(probe);
    const Result<OpHandle> started = f.service.start_remediate(PrerequisiteId::LinuxVulkan, DisconnectPolicy::Detached);
    REQUIRE_FALSE(started);
    CHECK(started.error().id == "integration.prerequisite_not_remediable");
    CHECK(f.rig.ops.live().empty());
}

TEST_CASE("a remediation re-checks and publishes what it found", "[integration][prerequisites]") {
    testing::FakePrereqs probe;
    probe.set({status("linux.linger", false), status("linux.vulkan", true)});
    Fixture f(probe);
    const Result<OpHandle> started = f.service.start_remediate(PrerequisiteId::LinuxLinger, DisconnectPolicy::Detached);
    REQUIRE(started);
    const Prerequisite fixed = test::completed_value<Prerequisite>(f.rig.wait(started->id()));
    CHECK(fixed.id == PrerequisiteId::LinuxLinger);
    CHECK(fixed.met);
    CHECK(probe.remediated() == std::vector<std::string>{"linux.linger"});

    const std::vector<PrerequisitesChanged> changes = f.changes();
    REQUIRE(changes.size() == 1);
    CHECK(changes[0].prerequisites.size() == 2);
}

TEST_CASE("a prerequisite the probe does not report is NotApplicable", "[integration][prerequisites]") {
    testing::FakePrereqs probe;
    probe.set({status("linux.vulkan", true)});
    Fixture f(probe);
    const Result<OpHandle> started = f.service.start_remediate(PrerequisiteId::MacRosetta, DisconnectPolicy::Detached);
    REQUIRE(started);
    CHECK(test::failure_of(f.rig.wait(started->id())).id == "integration.prerequisite_not_applicable");
    CHECK(probe.remediated().empty());
}

TEST_CASE("a failing remedy keeps its cause and publishes nothing", "[integration][prerequisites]") {
    testing::FakePrereqs probe;
    probe.set({status("mac.rosetta", false)});
    probe.faults().fail_next(testing::PrereqOperation::Remediate, test::fault("platform.softwareupdate_failed"));
    Fixture f(probe);
    const Result<OpHandle> started = f.service.start_remediate(PrerequisiteId::MacRosetta, DisconnectPolicy::Detached);
    REQUIRE(started);
    const Diagnostic failed = test::failure_of(f.rig.wait(started->id()));
    CHECK(failed.id == "integration.remediation_failed");
    REQUIRE(failed.causes.size() == 1);
    CHECK(failed.causes[0].id == "platform.softwareupdate_failed");
    CHECK(f.changes().empty());
}

TEST_CASE("an install that leaves it missing is StillMissing, but opening settings is not", "[integration][prerequisites]") {
    StubbornProbe probe({status("mac.rosetta", false), status("mac.local_network_hint", false)});
    Fixture f(probe);

    const Result<OpHandle> install = f.service.start_remediate(PrerequisiteId::MacRosetta, DisconnectPolicy::Detached);
    REQUIRE(install);
    CHECK(test::failure_of(f.rig.wait(install->id())).id == "integration.prerequisite_still_missing");
    CHECK(f.changes().size() == 1);

    const Result<OpHandle> settings =
        f.service.start_remediate(PrerequisiteId::MacLocalNetwork, DisconnectPolicy::Detached);
    REQUIRE(settings);
    const Prerequisite opened = test::completed_value<Prerequisite>(f.rig.wait(settings->id()));
    CHECK(opened.id == PrerequisiteId::MacLocalNetwork);
    CHECK_FALSE(opened.met);
    CHECK(probe.remedies() == 2);
    CHECK(f.changes().size() == 1);
}

TEST_CASE("an install already met does not run again", "[integration][prerequisites]") {
    StubbornProbe probe({status("mac.rosetta", true)});
    Fixture f(probe);
    const Result<OpHandle> started = f.service.start_remediate(PrerequisiteId::MacRosetta, DisconnectPolicy::Detached);
    REQUIRE(started);
    CHECK(test::completed_value<Prerequisite>(f.rig.wait(started->id())).met);
    CHECK(probe.remedies() == 0);
    CHECK(f.changes().empty());
}

TEST_CASE("a second call for the same id joins the remediation in progress", "[integration][prerequisites]") {
    StubbornProbe probe({status("linux.linger", false)});
    Fixture f(probe);
    test::Gate gate;
    probe.hold(gate.future());

    const Result<OpHandle> first = f.service.start_remediate(PrerequisiteId::LinuxLinger, DisconnectPolicy::Detached);
    const Result<OpHandle> second = f.service.start_remediate(PrerequisiteId::LinuxLinger, DisconnectPolicy::Detached);
    REQUIRE(first);
    REQUIRE(second);
    CHECK(first->id() == second->id());
    gate.open();
    CHECK(test::failure_of(f.rig.wait(first->id())).id == "integration.prerequisite_still_missing");
    CHECK(probe.remedies() == 1);
}

TEST_CASE("a remediation past its 10 minutes times out and a late result changes nothing", "[integration][prerequisites]") {
    StubbornProbe probe({status("mac.rosetta", false)});
    Fixture f(probe);
    test::Gate gate;
    probe.hold(gate.future());

    std::future<void> entered = probe.entered();
    const Result<OpHandle> started = f.service.start_remediate(PrerequisiteId::MacRosetta, DisconnectPolicy::Detached);
    REQUIRE(started);
    REQUIRE(entered.wait_for(std::chrono::seconds{20}) == std::future_status::ready);
    f.rig.strand.advance(std::chrono::minutes{9});
    CHECK_FALSE(f.rig.ops.outcome(started->id()));
    f.rig.strand.advance(std::chrono::minutes{1});
    REQUIRE(f.rig.ops.outcome(started->id()));
    CHECK(std::holds_alternative<TimedOut>(*f.rig.ops.outcome(started->id())));

    gate.open();
    // The re-check still publishes once the remedy returns.
    f.rig.strand.run_until([&] { return f.recorder.pump() > 0; });
    CHECK(std::holds_alternative<TimedOut>(*f.rig.ops.outcome(started->id())));

    // The timed out op no longer blocks a new one.
    const Result<OpHandle> again = f.service.start_remediate(PrerequisiteId::MacRosetta, DisconnectPolicy::Detached);
    REQUIRE(again);
    CHECK(again->id() != started->id());
    CHECK(test::failure_of(f.rig.wait(again->id())).id == "integration.prerequisite_still_missing");
}

TEST_CASE("a remediation after a cancelled one waits until that remedy returns", "[integration][prerequisites]") {
    StubbornProbe probe({status("mac.rosetta", false)});
    Fixture f(probe);
    test::Gate gate;
    probe.hold(gate.future());

    std::future<void> entered = probe.entered();
    const Result<OpHandle> first = f.service.start_remediate(PrerequisiteId::MacRosetta, DisconnectPolicy::Detached);
    REQUIRE(first);
    REQUIRE(entered.wait_for(std::chrono::seconds{20}) == std::future_status::ready);
    REQUIRE(f.rig.ops.cancel(first->id(), CancelReason::User));

    const Result<OpHandle> second = f.service.start_remediate(PrerequisiteId::MacRosetta, DisconnectPolicy::Detached);
    REQUIRE(second);
    CHECK(second->id() != first->id());
    // The other worker is free: had the second remedy been submitted, it would hold that worker on the gate.
    std::promise<void> idle;
    f.workers.submit<int>(
        [&idle](CancelToken) -> Result<int> {
            idle.set_value();
            return 0;
        },
        {}, f.rig.strand, [](Result<int>) {});
    REQUIRE(idle.get_future().wait_for(std::chrono::seconds{20}) == std::future_status::ready);
    CHECK(probe.remedies() == 0);

    gate.open();
    CHECK(test::failure_of(f.rig.wait(second->id())).id == "integration.prerequisite_still_missing");
    CHECK(std::holds_alternative<Cancelled>(*f.rig.ops.outcome(first->id())));
    CHECK(probe.remedies() == 2);
}

TEST_CASE("a waiting remediation cancelled before its turn never runs", "[integration][prerequisites]") {
    StubbornProbe probe({status("linux.linger", false)});
    Fixture f(probe);
    test::Gate gate;
    probe.hold(gate.future());

    std::future<void> entered = probe.entered();
    const Result<OpHandle> first = f.service.start_remediate(PrerequisiteId::LinuxLinger, DisconnectPolicy::Detached);
    REQUIRE(first);
    REQUIRE(entered.wait_for(std::chrono::seconds{20}) == std::future_status::ready);
    REQUIRE(f.rig.ops.cancel(first->id(), CancelReason::User));
    const Result<OpHandle> second = f.service.start_remediate(PrerequisiteId::LinuxLinger, DisconnectPolicy::Detached);
    REQUIRE(second);
    REQUIRE(f.rig.ops.cancel(second->id(), CancelReason::User));

    gate.open();
    // The first remedy's re-check is published once it returns, and its reply drops the waiting one.
    f.rig.strand.run_until([&] { return f.recorder.pump() > 0; });
    f.rig.strand.run_ready();
    CHECK(probe.remedies() == 1);
    CHECK(std::holds_alternative<Cancelled>(*f.rig.ops.outcome(second->id())));

    const Result<OpHandle> third = f.service.start_remediate(PrerequisiteId::LinuxLinger, DisconnectPolicy::Detached);
    REQUIRE(third);
    CHECK(test::failure_of(f.rig.wait(third->id())).id == "integration.prerequisite_still_missing");
    CHECK(probe.remedies() == 2);
}
