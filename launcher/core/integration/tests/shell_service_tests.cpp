#include <catch2/catch_test_macros.hpp>

#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "integration_test_support.hpp"
#include "reboot/contracts/ipc.hpp"
#include "reboot/foundation/cancel.hpp"
#include "reboot/foundation/executor.hpp"
#include "reboot/integration/shell_service.hpp"
#include "reboot/testing/fake_platform_paths.hpp"
#include "reboot/testing/fake_shell.hpp"
#include "reboot/testing/fake_system_info.hpp"

using namespace rb;
using namespace rb::integration;
using contracts::ipc::CallerContext;

namespace {

struct Fixture {
    explicit Fixture(DesktopCheck check = DesktopCheck::OsSession) : service(shell, system, check, workers, strand) {}

    [[nodiscard]] Result<void> wait(std::optional<Result<void>>& result) {
        strand.run_until([&] { return result.has_value(); });
        return *result;
    }

    [[nodiscard]] UniqueFunction<void(Result<void>)> into(std::optional<Result<void>>& result) {
        return [&result](Result<void> done) { result = std::move(done); };
    }

    ManualClock clock;
    test::WorkerStrand strand{clock};
    WorkerPool workers{1};
    testing::FakeShell shell;
    testing::FakeSystemInfo system;
    ShellService service;
};

[[nodiscard]] CallerContext same_session() { return CallerContext{.os_session = "1", .elevated = false, .display_env = {}}; }

[[nodiscard]] NativePath absolute(std::string_view leaf) { return testing::default_fake_root() / leaf; }

}  // namespace

TEST_CASE("an https link from the engine's session is opened on a worker", "[integration][shell]") {
    Fixture f;
    std::optional<Result<void>> result;
    f.service.open_url(same_session(), "https://reboot.example/help", {}, f.into(result));
    CHECK_FALSE(result);
    CHECK(f.wait(result));
    CHECK(f.shell.opened_urls() == std::vector<std::string>{"https://reboot.example/help"});
}

TEST_CASE("anything but an https link is refused before the shell", "[integration][shell]") {
    Fixture f;
    for (const std::string url : {"http://reboot.example", "file:///etc/passwd", "https://", "https://a b",
                                  "reboot://3f1c"}) {
        std::optional<Result<void>> result;
        f.service.open_url(same_session(), url, {}, f.into(result));
        // Never inside the call, even for a refusal.
        CHECK_FALSE(result);
        const Result<void> refused = f.wait(result);
        REQUIRE_FALSE(refused);
        CHECK(refused.error().id == "integration.url_not_https");
    }
    CHECK(f.shell.opened_urls().empty());
}

TEST_CASE("a relative path is refused for open and reveal", "[integration][shell]") {
    Fixture f;
    std::optional<Result<void>> opened;
    std::optional<Result<void>> revealed;
    f.service.open_path(same_session(), NativePath("logs"), {}, f.into(opened));
    f.service.reveal(same_session(), NativePath("logs/engine.log"), {}, f.into(revealed));
    CHECK(f.wait(opened).error().id == "integration.path_not_absolute");
    CHECK(f.wait(revealed).error().id == "integration.path_not_absolute");

    std::optional<Result<void>> ok;
    f.service.reveal(same_session(), absolute("logs"), {}, f.into(ok));
    CHECK(f.wait(ok));
    CHECK(f.shell.revealed() == std::vector{absolute("logs")});
    CHECK(f.shell.opened_paths().empty());
}

TEST_CASE("a caller in another OS session gets no window", "[integration][shell]") {
    Fixture f;
    std::optional<Result<void>> result;
    f.service.open_path(CallerContext{.os_session = "2", .elevated = false, .display_env = {}}, absolute("data"), {},
                        f.into(result));
    const Result<void> refused = f.wait(result);
    REQUIRE_FALSE(refused);
    CHECK(refused.error().id == "integration.engine_in_other_session");
    CHECK(f.shell.opened_paths().empty());
}

TEST_CASE("on Linux the caller needs a display, not the same session", "[integration][shell]") {
    Fixture f(DesktopCheck::Display);
    std::optional<Result<void>> none;
    f.service.open_url(CallerContext{.os_session = "", .elevated = false, .display_env = {{"DISPLAY", ""}, {"LANG", "C"}}},
                       "https://reboot.example", {}, f.into(none));
    CHECK(f.wait(none).error().id == "integration.no_display");

    std::optional<Result<void>> wayland;
    f.service.open_url(
        CallerContext{.os_session = "other", .elevated = false, .display_env = {{"WAYLAND_DISPLAY", "wayland-0"}}},
        "https://reboot.example", {}, f.into(wayland));
    CHECK(f.wait(wayland));
    CHECK(f.shell.opened_urls().size() == 1);
}

TEST_CASE("a shell failure keeps its cause and names the target", "[integration][shell]") {
    Fixture f;
    f.shell.faults().fail_next(testing::ShellOperation::OpenPath, test::fault("platform.shell_execute_failed"));
    std::optional<Result<void>> result;
    f.service.open_path(same_session(), absolute("data"), {}, f.into(result));
    const Result<void> failed = f.wait(result);
    REQUIRE_FALSE(failed);
    CHECK(failed.error().id == "integration.shell_failed");
    CHECK(failed.error().find_arg("target") != nullptr);
    REQUIRE(failed.error().causes.size() == 1);
    CHECK(failed.error().causes[0].id == "platform.shell_execute_failed");
}

TEST_CASE("a call cancelled before the worker runs it never reaches the shell", "[integration][shell]") {
    Fixture f;
    CancelSource source;
    source.cancel(CancelReason::Disconnect);
    std::optional<Result<void>> result;
    f.service.open_url(same_session(), "https://reboot.example", source.token(), f.into(result));
    const Result<void> cancelled = f.wait(result);
    REQUIRE_FALSE(cancelled);
    CHECK(cancelled.error().id == "integration.shell_cancelled");
    CHECK(cancelled.error().kind == ErrorKind::Cancelled);
    CHECK(f.shell.opened_urls().empty());
}
