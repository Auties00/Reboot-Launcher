#include <catch2/catch_test_macros.hpp>

#include <optional>
#include <string>

#include "reboot/engine/package_hooks.hpp"
#include "reboot/testing/fake_platform_paths.hpp"
#include "reboot/testing/fake_registrar.hpp"

using namespace rb;
using namespace rb::engine;
using ports::IntegrationKind;
using ports::IntegrationState;

namespace {

[[nodiscard]] NativePath engine_exe(const testing::FakePlatformPaths& paths) {
#ifdef _WIN32
    return paths.exe_dir() / "reboot-engine.exe";
#else
    return paths.exe_dir() / "reboot-engine";
#endif
}

}  // namespace

TEST_CASE("package hooks: an install writes the engine agent for this install's engine") {
    testing::FakePlatformPaths paths;
    testing::FakeRegistrar registrar;
    CHECK_FALSE(run_package_hook(ports::PackageHook::AfterInstall, registrar, paths));
    CHECK(registrar.applied_exe(IntegrationKind::EngineAgent) == engine_exe(paths));
}

TEST_CASE("package hooks: an update rewrites a stale agent and leaves one the user removed") {
    testing::FakePlatformPaths paths;
    testing::FakeRegistrar removed;
    CHECK_FALSE(run_package_hook(ports::PackageHook::AfterUpdate, removed, paths));
    CHECK_FALSE(removed.applied_exe(IntegrationKind::EngineAgent));

    testing::FakeRegistrar stale;
    stale.set_state(IntegrationKind::EngineAgent, IntegrationState::Stale);
    CHECK_FALSE(run_package_hook(ports::PackageHook::AfterUpdate, stale, paths));
    CHECK(stale.applied_exe(IntegrationKind::EngineAgent) == engine_exe(paths));
}

TEST_CASE("package hooks: an uninstall removes our entries, keeps another app's, and names a failure") {
    testing::FakePlatformPaths paths;
    testing::FakeRegistrar registrar;
    REQUIRE(registrar.apply(IntegrationKind::EngineAgent, engine_exe(paths)));
    REQUIRE(registrar.apply(IntegrationKind::Autostart, engine_exe(paths)));
    registrar.set_state(IntegrationKind::UrlScheme, IntegrationState::Foreign);
    CHECK_FALSE(run_package_hook(ports::PackageHook::BeforeUninstall, registrar, paths));
    CHECK(registrar.status(IntegrationKind::EngineAgent)->state == IntegrationState::Absent);
    CHECK(registrar.status(IntegrationKind::Autostart)->state == IntegrationState::Absent);
    CHECK(registrar.status(IntegrationKind::UrlScheme)->state == IntegrationState::Foreign);

    REQUIRE(registrar.apply(IntegrationKind::EngineAgent, engine_exe(paths)));
    registrar.faults().fail_next(testing::RegistrarOperation::Remove,
                                 make_diag(ErrorDomain::Platform, MessageId{"platform.access_denied"}).build());
    const std::optional<Diagnostic> failed = run_package_hook(ports::PackageHook::BeforeUninstall, registrar, paths);
    REQUIRE(failed);
    CHECK(failed->id == "integration.remove_failed");
}
