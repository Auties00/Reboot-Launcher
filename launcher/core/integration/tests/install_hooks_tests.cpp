#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <vector>

#include "integration_test_support.hpp"
#include "reboot/integration/install_integration.hpp"
#include "reboot/integration/uninstall_integration.hpp"
#include "reboot/testing/fake_registrar.hpp"

using namespace rb;
using namespace rb::integration;
using ports::IntegrationState;
using testing::RegistrarOperation;

namespace {

[[nodiscard]] IntegrationTargets windows_targets() {
    return IntegrationTargets{.flavor = EntryFlavor::Windows,
                              .gui_exe = NativePath("C:/Reboot/Reboot.exe"),
                              .engine_exe = NativePath("C:/Reboot/reboot-engine.exe")};
}

[[nodiscard]] const EntryStatus& of(const std::vector<EntryStatus>& statuses, IntegrationKind kind) {
    const auto found = std::ranges::find(statuses, kind, &EntryStatus::kind);
    REQUIRE(found != statuses.end());
    return *found;
}

}  // namespace

TEST_CASE("an install writes the engine agent; an update leaves an absent one", "[integration][hooks]") {
    testing::FakeRegistrar registrar;
    const EntryStatus updated = install_integration(registrar, windows_targets(), InstallHook::AfterUpdate);
    CHECK(updated.state == EntryState::Absent);
    CHECK_FALSE(registrar.applied_exe(IntegrationKind::EngineAgent));

    const EntryStatus installed = install_integration(registrar, windows_targets(), InstallHook::AfterInstall);
    CHECK(installed.kind == IntegrationKind::EngineAgent);
    CHECK(installed.state == EntryState::Ours);
    CHECK_FALSE(installed.detail);
    CHECK(registrar.applied_exe(IntegrationKind::EngineAgent) == windows_targets().engine_exe);
}

TEST_CASE("an update rewrites a stale agent and never a foreign one", "[integration][hooks]") {
    testing::FakeRegistrar registrar;
    registrar.set_state(IntegrationKind::EngineAgent, IntegrationState::Stale);
    CHECK(install_integration(registrar, windows_targets(), InstallHook::AfterUpdate).state == EntryState::Ours);

    registrar.set_state(IntegrationKind::EngineAgent, IntegrationState::Foreign);
    registrar.set_detail(IntegrationKind::EngineAgent, "D:/Other/reboot-engine.exe");
    for (const InstallHook hook : {InstallHook::AfterInstall, InstallHook::AfterUpdate}) {
        const EntryStatus kept = install_integration(registrar, windows_targets(), hook);
        CHECK(kept.state == EntryState::Foreign);
        CHECK(kept.target == "D:/Other/reboot-engine.exe");
    }
    const EntryStatus kept = install_integration(registrar, windows_targets(), InstallHook::AfterInstall);
    REQUIRE(kept.detail);
    CHECK(kept.detail->id == "integration.foreign_entry");
}

TEST_CASE("a failed agent write reports the registrar's error", "[integration][hooks]") {
    testing::FakeRegistrar registrar;
    registrar.faults().fail_next(RegistrarOperation::Apply, test::fault("platform.task_denied"));
    const EntryStatus failed = install_integration(registrar, windows_targets(), InstallHook::AfterInstall);
    CHECK(failed.state == EntryState::Absent);
    REQUIRE(failed.detail);
    CHECK(failed.detail->id == "integration.write_failed");
    REQUIRE(failed.detail->causes.size() == 1);
    CHECK(failed.detail->causes[0].id == "platform.task_denied");

    registrar.faults().fail_next(RegistrarOperation::Status, test::fault());
    const EntryStatus unknown = install_integration(registrar, windows_targets(), InstallHook::AfterInstall);
    CHECK(unknown.state == EntryState::Unknown);
    REQUIRE(unknown.detail);
    CHECK(unknown.detail->id == "integration.inspect_failed");
}

TEST_CASE("a registrar that cannot write the agent here makes it Unsupported", "[integration][hooks]") {
    testing::FakeRegistrar registrar;
    registrar.set_state(IntegrationKind::EngineAgent, IntegrationState::Stale);
    registrar.faults().fail_next(RegistrarOperation::Apply, test::fault("platform.unsupported", ErrorKind::Unsupported));
    const EntryStatus unsupported = install_integration(registrar, windows_targets(), InstallHook::AfterUpdate);
    CHECK(unsupported.state == EntryState::Unsupported);
    REQUIRE(unsupported.detail);
    CHECK(unsupported.detail->id == "integration.unsupported");
}

TEST_CASE("uninstall removes our entries and keeps foreign ones", "[integration][hooks]") {
    testing::FakeRegistrar registrar;
    const IntegrationTargets targets = windows_targets();
    REQUIRE(registrar.apply(IntegrationKind::EngineAgent, targets.engine_exe));
    REQUIRE(registrar.apply(IntegrationKind::Autostart, targets.engine_exe));
    registrar.set_state(IntegrationKind::UrlScheme, IntegrationState::Foreign);
    registrar.set_detail(IntegrationKind::UrlScheme, "\"D:/Old/Launcher.exe\" \"%1\"");

    const std::vector<EntryStatus> statuses = uninstall_integration(registrar, targets);
    REQUIRE(statuses.size() == kAllIntegrationKinds.size());
    CHECK(of(statuses, IntegrationKind::EngineAgent).state == EntryState::Absent);
    CHECK(of(statuses, IntegrationKind::Autostart).state == EntryState::Absent);
    CHECK(of(statuses, IntegrationKind::UrlScheme).state == EntryState::Foreign);
    CHECK_FALSE(of(statuses, IntegrationKind::UrlScheme).detail);
    // Windows has no menu entry of ours.
    CHECK(of(statuses, IntegrationKind::DesktopEntry).state == EntryState::Unsupported);
    for (const EntryStatus& status : statuses) CHECK_FALSE(status.declined);

    CHECK(registrar.status(IntegrationKind::UrlScheme)->state == IntegrationState::Foreign);
    CHECK(registrar.status(IntegrationKind::EngineAgent)->state == IntegrationState::Absent);
}

TEST_CASE("uninstall goes on past a failed removal", "[integration][hooks]") {
    testing::FakeRegistrar registrar;
    const IntegrationTargets targets = windows_targets();
    REQUIRE(registrar.apply(IntegrationKind::UrlScheme, *targets.gui_exe));
    REQUIRE(registrar.apply(IntegrationKind::EngineAgent, targets.engine_exe));
    registrar.faults().fail_next(RegistrarOperation::Remove, test::fault("platform.access_denied"));

    const std::vector<EntryStatus> statuses = uninstall_integration(registrar, targets);
    const EntryStatus& scheme = of(statuses, IntegrationKind::UrlScheme);
    REQUIRE(scheme.detail);
    CHECK(scheme.detail->id == "integration.remove_failed");
    CHECK(of(statuses, IntegrationKind::EngineAgent).state == EntryState::Absent);
    CHECK_FALSE(of(statuses, IntegrationKind::EngineAgent).detail);
}

TEST_CASE("an entry the registrar cannot remove here stays, reported as unsupported", "[integration][hooks]") {
    testing::FakeRegistrar registrar;
    const IntegrationTargets targets = windows_targets();
    REQUIRE(registrar.apply(IntegrationKind::EngineAgent, targets.engine_exe));
    registrar.faults().fail_next(RegistrarOperation::Remove,
                                 test::fault("platform.not_supported", ErrorKind::Unsupported));

    const std::vector<EntryStatus> statuses = uninstall_integration(registrar, targets);
    const EntryStatus& agent = of(statuses, IntegrationKind::EngineAgent);
    CHECK(agent.state == EntryState::Ours);
    REQUIRE(agent.detail);
    CHECK(agent.detail->id == "integration.unsupported");
    REQUIRE(agent.detail->causes.size() == 1);
    CHECK(agent.detail->causes[0].id == "platform.not_supported");
}
