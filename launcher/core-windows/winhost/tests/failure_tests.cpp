#include <catch2/catch_test_macros.hpp>

#include <string>

#include "reboot/os_windows/winhost/winhost_failure.hpp"

using namespace rb;
using namespace rb::os_windows::winhost;

TEST_CASE("each failure step has its own WhFatal name") {
    CHECK(step_name(FailureStep::Bootstrap) == "bootstrap");
    CHECK(step_name(FailureStep::Connect) == "connect");
    CHECK(step_name(FailureStep::Handshake) == "handshake");
    CHECK(step_name(FailureStep::Protocol) == "protocol");
    CHECK(step_name(FailureStep::Spawn) == "spawn");
    CHECK(step_name(FailureStep::Inject) == "inject");
    CHECK(step_name(FailureStep::Resume) == "resume");
    CHECK(step_name(FailureStep::Stop) == "stop");
    CHECK(step_name(FailureStep::Relay) == "relay");
}

TEST_CASE("to_fatal carries the step name and the os code") {
    const auto fatal = to_fatal({FailureStep::Spawn, 2});
    CHECK(fatal.step == "spawn");
    CHECK(fatal.os_code == 2);
    CHECK_FALSE(to_fatal({FailureStep::Protocol, std::nullopt}).os_code);
}

TEST_CASE("a failed reply is game_channel.request_failed with the code from inside the prefix") {
    const auto reply = failed_reply(9, "Inject", {FailureStep::Inject, 577});
    CHECK(reply.req_id == 9);
    CHECK_FALSE(reply.ok);
    REQUIRE(reply.error);
    CHECK(reply.error->id == "game_channel.request_failed");
    CHECK(reply.error->os_origin == SystemError::Origin::GuestWindows);
    CHECK(reply.error->os_code == 577);
    CHECK(reply.error->detail == std::string("inject"));

    const auto diag = contracts::common::to_diagnostic(*reply.error);
    CHECK(diag.domain == ErrorDomain::GameChannel);
    const Arg* role = diag.find_arg("role");
    const Arg* request = diag.find_arg("request");
    REQUIRE(role);
    REQUIRE(request);
    CHECK(std::get<std::string>(*role) == "winhost");
    CHECK(std::get<std::string>(*request) == "Inject");
}

TEST_CASE("a failed reply without an os code carries none") {
    const auto reply = failed_reply(1, "Resume", {FailureStep::Resume, std::nullopt});
    REQUIRE(reply.error);
    CHECK_FALSE(reply.error->os_code);
}
