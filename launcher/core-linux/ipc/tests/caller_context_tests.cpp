#include <catch2/catch_message.hpp>
#include <catch2/catch_test_macros.hpp>

#include <array>
#include <string>
#include <string_view>
#include <unistd.h>
#include <utility>
#include <vector>

#include "caller_facts.hpp"
#include "display_env_names.hpp"
#include "reboot/os_linux/ipc/linux_caller_context.hpp"
#include "reboot/testing/port_conformance.hpp"

using rb::os_linux::ipc::caller_context_from;
using rb::os_linux::ipc::CallerFacts;
using rb::os_linux::ipc::LinuxCallerContext;

namespace {

using Pairs = std::vector<std::pair<std::string, std::string>>;

constexpr std::array<std::string_view, 0> kNoEnvironment{};

CallerFacts desktop_facts() {
    return {.environment = kNoEnvironment, .session_id = "3\n", .login_uid = "1000\n", .euid = 1000, .uid = 1000};
}

}  // namespace

TEST_CASE("display_env keeps allow-listed, non-empty variables in environment order", "[caller_context]") {
    constexpr std::array<std::string_view, 11> kEnvironment{
        "PATH=/usr/bin",       "WAYLAND_DISPLAY=wayland-0", "DISPLAY=:0",       "LD_PRELOAD=/x.so",
        "XAUTHORITY=",         "LC_ALL=C.UTF-8",            "PIPEWIRE_RUNTIME_DIR=/run/user/1000",
        "SteamAppId=1",        "DISPLAY=:1",                "=orphan",          "WINEPREFIX=/p",
    };
    CallerFacts facts = desktop_facts();
    facts.environment = kEnvironment;
    const rb::ports::CallerContext context = caller_context_from(facts);
    CHECK(context.display_env == Pairs{{"WAYLAND_DISPLAY", "wayland-0"},
                                       {"DISPLAY", ":0"},
                                       {"LC_ALL", "C.UTF-8"},
                                       {"PIPEWIRE_RUNTIME_DIR", "/run/user/1000"}});
}

TEST_CASE("an empty first value hides a later duplicate, as getenv sees it", "[caller_context]") {
    constexpr std::array<std::string_view, 2> kEnvironment{"DISPLAY=", "DISPLAY=:0"};
    CallerFacts facts = desktop_facts();
    facts.environment = kEnvironment;
    CHECK(caller_context_from(facts).display_env.empty());
}

TEST_CASE("the allow-list never admits Steam, loader, Wine or umu variables", "[caller_context]") {
    for (const std::string_view name : {"SteamGameId", "LD_LIBRARY_PATH", "WINEDLLOVERRIDES", "UMU_ID", "HOME"})
        CHECK_FALSE(rb::os_linux::ipc::is_display_env_name(name));
}

TEST_CASE("a desktop login is interactive, not elevated, with its audit session", "[caller_context]") {
    const rb::ports::CallerContext context = caller_context_from(desktop_facts());
    CHECK(context.os_session == "3");
    CHECK(context.interactive);
    CHECK_FALSE(context.elevated);
}

TEST_CASE("an unset audit session and login uid read as absent", "[caller_context]") {
    CallerFacts facts = desktop_facts();
    facts.session_id = "4294967295";
    facts.login_uid = "4294967295";
    const rb::ports::CallerContext context = caller_context_from(facts);
    CHECK(context.os_session.empty());
    CHECK_FALSE(context.interactive);

    facts.session_id = std::nullopt;
    facts.login_uid = std::nullopt;
    CHECK(caller_context_from(facts).os_session.empty());
    CHECK_FALSE(caller_context_from(facts).interactive);

    facts.session_id = "x1";
    facts.login_uid = "-1";
    CHECK(caller_context_from(facts).os_session.empty());
    CHECK_FALSE(caller_context_from(facts).interactive);
}

TEST_CASE("sudo and setuid are elevated; a root login and a container's root are not", "[caller_context]") {
    CallerFacts facts = desktop_facts();
    facts.euid = 0;
    CHECK(caller_context_from(facts).elevated);

    facts.uid = 0;
    CHECK(caller_context_from(facts).elevated);

    facts.login_uid = "0";
    CHECK_FALSE(caller_context_from(facts).elevated);

    facts.login_uid = std::nullopt;
    CHECK_FALSE(caller_context_from(facts).elevated);

    facts.uid = 1000;
    CHECK(caller_context_from(facts).elevated);
}

TEST_CASE("detect reads this process without failing", "[caller_context]") {
    LinuxCallerContext probe = LinuxCallerContext::detect();
    const rb::ports::CallerContext context = probe.capture();
    for (const auto& [name, value] : context.display_env) {
        CHECK(rb::os_linux::ipc::is_display_env_name(name));
        CHECK_FALSE(value.empty());
    }
    probe.allow_foreground(1);
    CHECK(probe.capture().display_env == context.display_env);
}

TEST_CASE("LinuxCallerContext passes the caller context conformance suite", "[caller_context]") {
    LinuxCallerContext probe = LinuxCallerContext::detect();
    const rb::testing::ConformanceReport report =
        rb::testing::run_caller_context_conformance(probe, static_cast<rb::u32>(::getpid()));
    INFO(report.describe());
    CHECK(report.passed());
}
