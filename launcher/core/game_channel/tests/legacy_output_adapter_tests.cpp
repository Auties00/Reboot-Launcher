#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "reboot/game_channel/legacy_output_adapter.hpp"
#include "reboot/game_channel/lifecycle_markers.hpp"
#include "reboot/process/line_reader.hpp"

namespace reboot::game_channel {
namespace {

constexpr std::string_view kLogin = "LogOnlineAccount: [UOnlineAccountCommon::ContinueLoggingIn] Login (Completed)\n";
constexpr std::string_view kShutdown = "LogOnline: FOnlineSubsystemGoogleCommon::Shutdown()\n";
constexpr std::string_view kCorrupt = "LogWindows:Error: Fatal error!\n";
constexpr std::string_view kRefused = "LogOnline: Unable to login to Fortnite servers\n";
constexpr std::string_view kNoBackend = "LogHttp: request to port 3551 failed: Connection refused\n";

[[nodiscard]] std::span<const u8> bytes(std::string_view text) {
    return {reinterpret_cast<const u8*>(text.data()), text.size()};
}

struct Adapter {
    Adapter() : adapter(std::make_unique<LegacyOutputAdapter>(SessionId{}, builtin_lifecycle_markers(),
                                                               [this](GameLifecycleEvent event) { events.push_back(std::move(event)); })) {}

    void feed(OutputSource source, std::string_view text) { adapter->feed(source, bytes(text)); }

    std::vector<GameLifecycleEvent> events;
    std::unique_ptr<LegacyOutputAdapter> adapter;
};

TEST_CASE("the builtin markers rank the end of a session above its progress", "[game_channel][markers]") {
    const LifecycleMarkers& markers = builtin_lifecycle_markers();
    CHECK(markers.version == 1);
    CHECK(markers.match("[UOnlineAccountCommon::ContinueLoggingIn] (Completed)") == LegacyMarker::LoginCompleted);
    CHECK_FALSE(markers.match("[UOnlineAccountCommon::ContinueLoggingIn] (Pending)"));
    CHECK(markers.match("Critical error in [UOnlineAccountCommon::ContinueLoggingIn] (Completed)") == LegacyMarker::CorruptBuild);
    CHECK(markers.match("HTTP 400 response from https://x FOnlineSubsystemGoogleCommon::Shutdown()") == LegacyMarker::Shutdown);
    CHECK(markers.match("Network failure when attempting to check platform restrictions") == LegacyMarker::CannotConnect);
    CHECK(markers.match("UOnlineAccountCommon::ForceLogout") == LegacyMarker::AuthFailure);
    CHECK_FALSE(markers.match("LogInit: nothing to see"));
}

TEST_CASE("marker lines become the events our DLL would send", "[game_channel][legacy]") {
    Adapter a;
    a.feed(OutputSource::Stdout, kLogin);
    a.feed(OutputSource::Stdout, kShutdown);
    REQUIRE(a.events.size() == 2);
    CHECK(std::holds_alternative<LoggedIn>(a.events[0]));
    const auto* exit = std::get_if<ExitRequested>(&a.events[1]);
    REQUIRE(exit != nullptr);
    CHECK(exit->kind == contracts::game_client::ExitKind::RequestExit);
    CHECK(exit->code == 0);
    CHECK(exit->phase.empty());
}

TEST_CASE("each fatal marker names its cause", "[game_channel][legacy]") {
    const auto cause_of = [](std::string_view line) {
        Adapter a;
        a.feed(OutputSource::Stderr, line);
        REQUIRE(a.events.size() == 1);
        const auto* fatal = std::get_if<SessionFatal>(&a.events[0]);
        REQUIRE(fatal != nullptr);
        CHECK(fatal->step.empty());
        return fatal->cause;
    };
    CHECK(cause_of(kCorrupt) == FatalCause::CorruptBuild);
    CHECK(cause_of(kRefused) == FatalCause::AuthFailure);
    CHECK(cause_of(kNoBackend) == FatalCause::CannotConnect);
}

TEST_CASE("stdout and the UE log repeating a line give one event", "[game_channel][legacy]") {
    Adapter a;
    a.feed(OutputSource::Stdout, kLogin);
    a.feed(OutputSource::UeLog, kLogin);
    a.feed(OutputSource::Stdout, kLogin);
    CHECK(a.events.size() == 1);
}

TEST_CASE("nothing follows a fatal or an exit", "[game_channel][legacy]") {
    Adapter fatal;
    fatal.feed(OutputSource::UeLog, kRefused);
    fatal.feed(OutputSource::Stdout, kLogin);
    fatal.feed(OutputSource::Stdout, kShutdown);
    CHECK(fatal.events.size() == 1);

    Adapter exited;
    exited.feed(OutputSource::Stdout, kShutdown);
    exited.feed(OutputSource::Stdout, kCorrupt);
    CHECK(exited.events.size() == 1);
}

TEST_CASE("a marker split across chunks or left unterminated still counts", "[game_channel][legacy]") {
    Adapter a;
    a.feed(OutputSource::UeLog, kLogin.substr(0, 20));
    CHECK(a.events.empty());
    a.feed(OutputSource::UeLog, kLogin.substr(20));
    CHECK(a.events.size() == 1);

    a.feed(OutputSource::Stderr, "LogOnline: FOnlineSubsystemGoogleCommon::Shut");
    a.feed(OutputSource::Stderr, "down()");
    CHECK(a.events.size() == 1);
    a.adapter->finish(OutputSource::Stderr);
    REQUIRE(a.events.size() == 2);
    CHECK(std::holds_alternative<ExitRequested>(a.events[1]));
}

TEST_CASE("a line cut at the length cap is matched only by its first part", "[game_channel][legacy]") {
    const std::string padding(process::LineReader::kMaxLineBytes, 'x');
    Adapter tail_only;
    tail_only.feed(OutputSource::Stdout, padding + std::string(kShutdown));
    CHECK(tail_only.events.empty());

    Adapter head;
    head.feed(OutputSource::Stdout, std::string(kShutdown.substr(0, kShutdown.size() - 1)) + padding + "\n");
    REQUIRE(head.events.size() == 1);
    CHECK(std::holds_alternative<ExitRequested>(head.events[0]));

    // The line after a cut one is matched again.
    Adapter next;
    next.feed(OutputSource::Stdout, padding + "\n" + std::string(kLogin));
    CHECK(next.events.size() == 1);
}

TEST_CASE("sources keep their own partial lines", "[game_channel][legacy]") {
    Adapter a;
    a.feed(OutputSource::Stdout, "[UOnlineAccountCommon::ContinueLoggingIn] ");
    a.feed(OutputSource::Stderr, "(Completed)\n");
    a.feed(OutputSource::Stdout, "\n");
    CHECK(a.events.empty());
}

TEST_CASE("a handler may drop the adapter while lines are pending", "[game_channel][legacy]") {
    std::unique_ptr<LegacyOutputAdapter> adapter;
    int calls = 0;
    adapter = std::make_unique<LegacyOutputAdapter>(SessionId{}, builtin_lifecycle_markers(), [&](GameLifecycleEvent) {
        ++calls;
        adapter.reset();
    });
    const std::string both = std::string(kLogin) + std::string(kShutdown);
    adapter->feed(OutputSource::Stdout, bytes(both));
    CHECK(calls == 1);
    CHECK_FALSE(adapter);
}

}  // namespace
}  // namespace reboot::game_channel
