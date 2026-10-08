#include <catch2/catch_test_macros.hpp>

#include <string>
#include <utility>

#include "reboot/integration/integration_policy.hpp"
#include "reboot/integration/integration_targets.hpp"
#include "reboot/ports/os_services.hpp"

using namespace reboot;
using namespace reboot::integration;

namespace {

[[nodiscard]] ports::IntegrationStatus ours(IntegrationKind kind, std::string detail) {
    return {kind, ports::IntegrationState::Ours, std::move(detail)};
}

}  // namespace

TEST_CASE("the registrar's Absent, Foreign and Stale verdicts are kept", "[integration][policy]") {
    for (const auto& [state, expected] :
         {std::pair{ports::IntegrationState::Absent, EntryState::Absent},
          std::pair{ports::IntegrationState::Foreign, EntryState::Foreign},
          std::pair{ports::IntegrationState::Stale, EntryState::Stale}})
        CHECK(classify({IntegrationKind::UrlScheme, state, "x"}, EntryFlavor::Windows) == expected);
}

TEST_CASE("our entry with the expected arguments is Ours whatever the program path", "[integration][policy]") {
    // MSVC mis-stringizes raw literals holding backslashes inside Catch's macros, so they stay outside.
    const std::string windows = R"("C:\Users\a b\Reboot\current\Reboot.exe" --activate-url "%1")";
    CHECK(classify(ours(IntegrationKind::UrlScheme, windows), EntryFlavor::Windows) == EntryState::Ours);
    CHECK(classify(ours(IntegrationKind::UrlScheme, R"("/home/a/Reboot.AppImage" --activate-url %u)"),
                   EntryFlavor::FreeDesktop) == EntryState::Ours);
    CHECK(classify(ours(IntegrationKind::Autostart, R"("/opt/reboot/current/reboot-engine" run --origin=service-manager)"),
                   EntryFlavor::FreeDesktop) == EntryState::Ours);
}

TEST_CASE("a wrapper before the program is allowed", "[integration][policy]") {
    CHECK(classify(ours(IntegrationKind::Autostart,
                        R"(env REBOOT_LAUNCHER_HOME=/data "/opt/reboot/reboot-engine" run --origin=service-manager)"),
                   EntryFlavor::FreeDesktop) == EntryState::Ours);
}

TEST_CASE("our entry that drops the link placeholder is Stale", "[integration][policy]") {
    const std::string no_placeholder = R"("C:\Reboot\Reboot.exe" --activate-url)";
    const std::string no_flag = R"("C:\Reboot\Reboot.exe" "%1")";
    const std::string unbalanced = R"("C:\Reboot\Reboot.exe)";
    CHECK(classify(ours(IntegrationKind::UrlScheme, no_placeholder), EntryFlavor::Windows) == EntryState::Stale);
    CHECK(classify(ours(IntegrationKind::UrlScheme, no_flag), EntryFlavor::Windows) == EntryState::Stale);
    CHECK(classify(ours(IntegrationKind::UrlScheme, unbalanced), EntryFlavor::Windows) == EntryState::Stale);
}

TEST_CASE("an entry the user turned off is Disabled, not Stale", "[integration][policy]") {
    CHECK(classify(ours(IntegrationKind::Autostart, "disabled"), EntryFlavor::Windows) == EntryState::Disabled);
    CHECK(classify(ours(IntegrationKind::Autostart, "disabled"), EntryFlavor::FreeDesktop) == EntryState::Disabled);
    CHECK(classify(ours(IntegrationKind::Autostart, "requires_approval"), EntryFlavor::Apple) ==
          EntryState::AwaitingApproval);
    CHECK(classify(ours(IntegrationKind::EngineAgent, "requires_approval"), EntryFlavor::Apple) ==
          EntryState::AwaitingApproval);
}

TEST_CASE("entries without a checked command keep the registrar's Ours", "[integration][policy]") {
    CHECK(classify(ours(IntegrationKind::UrlScheme, "/Applications/Reboot Launcher.app"), EntryFlavor::Apple) ==
          EntryState::Ours);
    CHECK(classify(ours(IntegrationKind::EngineAgent, "enabled"), EntryFlavor::FreeDesktop) == EntryState::Ours);
}

TEST_CASE("only Ours, Disabled and AwaitingApproval count as registered", "[integration][policy]") {
    CHECK(is_registered(EntryState::Ours));
    CHECK(is_registered(EntryState::Disabled));
    CHECK(is_registered(EntryState::AwaitingApproval));
    for (const EntryState state : {EntryState::Absent, EntryState::Foreign, EntryState::Stale, EntryState::Unsupported,
                                   EntryState::Unknown})
        CHECK_FALSE(is_registered(state));
}

TEST_CASE("reconcile rewrites Stale entries of every kind", "[integration][policy]") {
    for (const IntegrationKind kind : kAllIntegrationKinds)
        CHECK(reconcile_action(kind, EntryState::Stale, false) == ReconcileAction::Write);
}

TEST_CASE("reconcile creates missing entries except Autostart", "[integration][policy]") {
    CHECK(reconcile_action(IntegrationKind::UrlScheme, EntryState::Absent, false) == ReconcileAction::Write);
    CHECK(reconcile_action(IntegrationKind::EngineAgent, EntryState::Absent, false) == ReconcileAction::Write);
    CHECK(reconcile_action(IntegrationKind::DesktopEntry, EntryState::Absent, false) == ReconcileAction::Write);
    CHECK(reconcile_action(IntegrationKind::Autostart, EntryState::Absent, false) == ReconcileAction::Leave);
}

TEST_CASE("reconcile leaves declined kinds and entries it must not touch", "[integration][policy]") {
    CHECK(reconcile_action(IntegrationKind::UrlScheme, EntryState::Absent, true) == ReconcileAction::Leave);
    CHECK(reconcile_action(IntegrationKind::UrlScheme, EntryState::Stale, true) == ReconcileAction::Leave);
    for (const EntryState state : {EntryState::Ours, EntryState::Disabled, EntryState::AwaitingApproval,
                                   EntryState::Foreign, EntryState::Unsupported, EntryState::Unknown})
        CHECK(reconcile_action(IntegrationKind::Autostart, state, false) == ReconcileAction::Leave);
}

TEST_CASE("kinds without a program in this install are unsupported", "[integration][targets]") {
    const IntegrationTargets windows{EntryFlavor::Windows, NativePath("C:/Reboot/Reboot.exe"),
                                     NativePath("C:/Reboot/reboot-engine.exe")};
    CHECK(entry_program(IntegrationKind::UrlScheme, windows) == windows.gui_exe);
    CHECK(entry_program(IntegrationKind::Autostart, windows) == windows.engine_exe);
    CHECK(entry_program(IntegrationKind::EngineAgent, windows) == windows.engine_exe);
    CHECK_FALSE(entry_program(IntegrationKind::DesktopEntry, windows));

    const IntegrationTargets linux_cli_only{EntryFlavor::FreeDesktop, std::nullopt,
                                            NativePath("/opt/reboot/reboot-engine")};
    CHECK_FALSE(entry_program(IntegrationKind::UrlScheme, linux_cli_only));
    CHECK_FALSE(entry_program(IntegrationKind::DesktopEntry, linux_cli_only));
    CHECK(entry_program(IntegrationKind::EngineAgent, linux_cli_only) == linux_cli_only.engine_exe);
}
