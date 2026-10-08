#include <catch2/catch_test_macros.hpp>
#include <chrono>

#include "connect_settings.hpp"
#include "messages.hpp"
#include "reboot/client.h"

using namespace reboot;
using namespace reboot::client;

namespace {

[[nodiscard]] rb_ctx_options options() {
    rb_ctx_options out{};
    out.struct_size = sizeof(rb_ctx_options);
    out.client_kind = RB_CLIENT_CLI;
    return out;
}

}  // namespace

TEST_CASE("minimal options take the defaults", "[client][settings]") {
    const rb_ctx_options in = options();
    const auto settings = read_connect_settings(&in);
    REQUIRE(settings);
    CHECK_FALSE(settings->data_root);
    CHECK(settings->client_kind == contracts::ipc::ClientKind::Cli);
    CHECK(settings->launch_mode == ipc::LaunchMode::Autostart);
    CHECK(settings->connect_deadline == default_deadline(OpKind::EngineConnect));
}

TEST_CASE("every field is read and copied", "[client][settings]") {
    rb_ctx_options in = options();
    in.data_root = "/srv/reboot";
    in.launch_mode = RB_LAUNCH_CONNECT_ONLY;
    in.connect_deadline_ms = 2500;
    const auto settings = read_connect_settings(&in);
    REQUIRE(settings);
    CHECK(settings->data_root == NativePath{u8"/srv/reboot"});
    CHECK(settings->launch_mode == ipc::LaunchMode::ConnectOnly);
    CHECK(settings->connect_deadline == std::chrono::milliseconds{2500});
}

TEST_CASE("bad options fail with client.invalid_argument", "[client][settings]") {
    CHECK(read_connect_settings(nullptr).error().is(msg::kInvalidArgument));

    rb_ctx_options too_small = options();
    too_small.struct_size = sizeof(rb_ctx_options) - 1;
    CHECK(read_connect_settings(&too_small).error().is(msg::kInvalidArgument));

    rb_ctx_options kind = options();
    kind.client_kind = RB_CLIENT_TEST + 1;
    CHECK(read_connect_settings(&kind).error().is(msg::kInvalidArgument));

    rb_ctx_options mode = options();
    mode.launch_mode = RB_LAUNCH_CONNECT_ONLY + 1;
    CHECK(read_connect_settings(&mode).error().is(msg::kInvalidArgument));

    rb_ctx_options empty_root = options();
    empty_root.data_root = "";
    CHECK(read_connect_settings(&empty_root).error().is(msg::kInvalidArgument));

    rb_ctx_options bad_utf8 = options();
    bad_utf8.data_root = "\xC3";
    CHECK(read_connect_settings(&bad_utf8).error().is(msg::kInvalidArgument));
}

TEST_CASE("fields of a newer minor are accepted only while they are zero", "[client][settings]") {
    struct NewerOptions {
        rb_ctx_options base;
        u64 appended = 0;
    };
    NewerOptions newer{options()};
    newer.base.struct_size = sizeof(NewerOptions);
    CHECK(read_connect_settings(&newer.base));

    newer.appended = 1;
    CHECK(read_connect_settings(&newer.base).error().is(msg::kAbiMismatch));
}
