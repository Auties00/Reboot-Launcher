#include <any>
#include <catch2/catch_test_macros.hpp>
#include <map>
#include <string>

#include "browser_test_support.hpp"
#include "reboot/browser/join_prompts.hpp"
#include "reboot/browser/join_service.hpp"
#include "reboot/foundation/user_request.hpp"

using namespace rb;
using namespace rb::browser;
using namespace rb::browser::test;
using namespace std::chrono_literals;

namespace {

struct JoinRig : SessionRig {
    JoinRig()
        : join(session, own, rt.requests(), rt.ops(), rt.clock(), [this](RequestId request) -> std::optional<SecretString> {
              const auto it = passwords.find(request);
              if (it == passwords.end()) return std::nullopt;
              SecretString password(it->second);
              passwords.erase(it);
              return password;
          }) {}

    [[nodiscard]] std::optional<UserRequest> prompt(UserRequestKind kind) {
        for (UserRequest& request : rt.requests().pending())
            if (request.kind == kind) return request;
        return std::nullopt;
    }

    void answer_resolve(Edge& edge, std::optional<wire::ListEntry> listed) {
        const wire::Resolve resolve = edge.expect<wire::Resolve>();
        wire::ResolveResult result{resolve.req_id, std::nullopt};
        if (listed) result.details = wire::EntryDetails{*listed, "A server", kEdgeTimeMs};
        edge.send(result);
        rt.run_until_idle();
    }

    FakeOwnServers own;
    std::map<RequestId, std::string> passwords;
    JoinService join;
};

[[nodiscard]] JoinRequest request(u8 seed, JoinConfirmation confirmation = JoinConfirmation::Ask) {
    return JoinRequest{server_id(seed), confirmation, *GameVersion::parse("8.51")};
}

}  // namespace

TEST_CASE("a confirmed join returns the granted IPv4 endpoint", "[browser][join]") {
    JoinRig rig;
    BrowserLease lease = rig.session.acquire();
    Edge& edge = rig.connect();
    auto [handle, op] = rig.rt.ops().create<int>(OpKind::Play, DisconnectPolicy::Detached, std::nullopt);
    Capture<Result<JoinOutcome>> done;
    REQUIRE(rig.join.join(request(1), op, std::nullopt, done.callback()));
    rig.rt.run_until_idle();
    rig.answer_resolve(edge, entry(1, 1, "Server"));

    const auto confirm = rig.prompt(UserRequestKind::ConfirmJoin);
    REQUIRE(confirm);
    CHECK(confirm->op == handle.id());
    CHECK(std::any_cast<ConfirmJoinPrompt>(confirm->payload).server.row.name == "Server");
    CHECK(rig.rt.requests().respond(confirm->id, std::string("yes")).error().id == "browser.invalid_answer");
    REQUIRE(rig.rt.requests().respond(confirm->id, ConfirmJoinAnswer{true}));

    const wire::Join join = edge.expect<wire::Join>();
    CHECK_FALSE(join.password);
    edge.send(wire::JoinGrant{join.req_id, {10, 1, 2, 3}, 7778, {9, 9}, kEdgeTimeMs + 60'000});
    rig.rt.run_until_idle();
    REQUIRE(done.calls == 1);
    REQUIRE(*done.result);
    const JoinOutcome& outcome = **done.result;
    CHECK(outcome.endpoint == Endpoint{ipv4(10, 1, 2, 3), Port{7778}});
    CHECK(outcome.ticket == std::vector<u8>{9, 9});
    CHECK(outcome.ticket_expires_at == std::chrono::system_clock::time_point(60s));
    CHECK(outcome.server.description == "A server");
}

TEST_CASE("own servers are refused before anything is sent", "[browser][join]") {
    JoinRig rig;
    rig.own.ids.push_back(server_id(1));
    auto [handle, op] = rig.rt.ops().create<int>(OpKind::Play, DisconnectPolicy::Detached, std::nullopt);
    const Result<void> started = rig.join.join(request(1), op, std::nullopt, [](Result<JoinOutcome>) {});
    REQUIRE_FALSE(started);
    CHECK(started.error().id == "browser.join_own_server");
    CHECK(rig.join.start_join(request(1), DisconnectPolicy::Detached).error().id == "browser.join_own_server");
    rig.rt.run_until_idle();
    CHECK(rig.quic.connections().empty());
}

TEST_CASE("a version mismatch fails before ConfirmJoin and before Join", "[browser][join]") {
    JoinRig rig;
    BrowserLease lease = rig.session.acquire();
    Edge& edge = rig.connect();
    auto [handle, op] = rig.rt.ops().create<int>(OpKind::Play, DisconnectPolicy::Detached, std::nullopt);
    Capture<Result<JoinOutcome>> done;
    REQUIRE(rig.join.join(request(1), op, std::nullopt, done.callback()));
    rig.rt.run_until_idle();
    rig.answer_resolve(edge, entry(1, 1, "Old", "7.40"));
    REQUIRE(done.calls == 1);
    const Diagnostic& error = done.result->error();
    CHECK(error.id == "browser.join_version_mismatch");
    CHECK(std::get<std::string>(*error.find_arg("version")) == "7.40");
    CHECK(std::get<std::string>(*error.find_arg("local_version")) == "8.51");
    CHECK(rig.rt.requests().pending().empty());
    CHECK(edge.idle());
}

TEST_CASE("unknown servers and declined prompts fail the join", "[browser][join]") {
    JoinRig rig;
    BrowserLease lease = rig.session.acquire();
    Edge& edge = rig.connect();
    auto [handle, op] = rig.rt.ops().create<int>(OpKind::Play, DisconnectPolicy::Detached, std::nullopt);

    Capture<Result<JoinOutcome>> missing;
    REQUIRE(rig.join.join(request(1), op, std::nullopt, missing.callback()));
    rig.rt.run_until_idle();
    rig.answer_resolve(edge, std::nullopt);
    CHECK(missing.result->error().id == "browser.server_not_found");

    Capture<Result<JoinOutcome>> declined;
    REQUIRE(rig.join.join(request(2), op, std::nullopt, declined.callback()));
    rig.rt.run_until_idle();
    rig.answer_resolve(edge, entry(1, 2, "Two"));
    REQUIRE(rig.rt.requests().respond(rig.prompt(UserRequestKind::ConfirmJoin)->id, ConfirmJoinAnswer{false}));
    REQUIRE(declined.calls == 1);
    CHECK(declined.result->error().id == "browser.join_refused");
    CHECK(edge.idle());
}

TEST_CASE("a protected server asks for the password and again after a wrong one", "[browser][join]") {
    JoinRig rig;
    BrowserLease lease = rig.session.acquire();
    Edge& edge = rig.connect();
    auto [handle, op] = rig.rt.ops().create<int>(OpKind::Play, DisconnectPolicy::Detached, std::nullopt);
    Capture<Result<JoinOutcome>> done;
    REQUIRE(rig.join.join(request(1, JoinConfirmation::AlreadyConfirmed), op, std::nullopt, done.callback()));
    rig.rt.run_until_idle();
    rig.answer_resolve(edge, entry(1, 1, "Locked", "8.51", 0,
                                   wire::entry_flag::online | wire::entry_flag::reachable | wire::entry_flag::has_password));

    auto ask = rig.prompt(UserRequestKind::NeedsJoinPassword);
    REQUIRE(ask);
    CHECK_FALSE(std::any_cast<NeedsJoinPasswordPrompt>(ask->payload).retry);
    CHECK(rig.rt.requests().respond(ask->id, JoinPasswordProvided{}).error().id == "browser.join_password_missing");
    rig.passwords[ask->id] = "wrong";
    REQUIRE(rig.rt.requests().respond(ask->id, JoinPasswordProvided{}));
    const wire::Join first = edge.expect<wire::Join>();
    CHECK(first.password == "wrong");
    edge.send(wire::Error{first.req_id, wire::ErrorCode::wrong_password, {}, 0});
    rig.rt.run_until_idle();

    ask = rig.prompt(UserRequestKind::NeedsJoinPassword);
    REQUIRE(ask);
    CHECK(std::any_cast<NeedsJoinPasswordPrompt>(ask->payload).retry);
    rig.passwords[ask->id] = "right";
    REQUIRE(rig.rt.requests().respond(ask->id, JoinPasswordProvided{}));
    const wire::Join second = edge.expect<wire::Join>();
    CHECK(second.password == "right");
    edge.send(wire::JoinGrant{second.req_id, {10, 0, 0, 7}, 7777, {}, 0});
    rig.rt.run_until_idle();
    REQUIRE(done.calls == 1);
    CHECK((*done.result)->endpoint.port == Port{7777});
}

TEST_CASE("an IPv6 grant is refused with the granted address", "[browser][join]") {
    JoinRig rig;
    BrowserLease lease = rig.session.acquire();
    Edge& edge = rig.connect();
    auto [handle, op] = rig.rt.ops().create<int>(OpKind::Play, DisconnectPolicy::Detached, std::nullopt);
    Capture<Result<JoinOutcome>> done;
    REQUIRE(rig.join.join(request(1, JoinConfirmation::AlreadyConfirmed), op, std::nullopt, done.callback()));
    rig.rt.run_until_idle();
    rig.answer_resolve(edge, entry(1, 1, "Six"));
    const wire::Join join = edge.expect<wire::Join>();
    wire::Bytes v6(16, 0);
    v6[0] = 0x20;
    v6[1] = 0x01;
    v6[15] = 1;
    edge.send(wire::JoinGrant{join.req_id, v6, 7777, {}, 0});
    rig.rt.run_until_idle();
    const Diagnostic& error = done.result->error();
    CHECK(error.id == "browser.unsupported_address_family");
    CHECK(std::get<std::string>(*error.find_arg("address")) == "[2001::1]:7777");
}

TEST_CASE("joins are paced per server, and RATE_LIMITED holds that server off", "[browser][join]") {
    JoinRig rig;
    BrowserLease lease = rig.session.acquire();
    Edge& edge = rig.connect();
    auto [handle, op] = rig.rt.ops().create<int>(OpKind::Play, DisconnectPolicy::Detached, std::nullopt);
    for (int attempt = 0; attempt < 5; ++attempt) {
        Capture<Result<JoinOutcome>> done;
        REQUIRE(rig.join.join(request(1, JoinConfirmation::AlreadyConfirmed), op, std::nullopt, done.callback()));
        rig.rt.run_until_idle();
        rig.answer_resolve(edge, entry(1, 1, "Busy"));
        const wire::Join join = edge.expect<wire::Join>();
        edge.send(wire::Error{join.req_id, wire::ErrorCode::unavailable, {}, 0});
        rig.rt.run_until_idle();
        CHECK(done.result->error().id == "browser.server_offline");
    }
    Capture<Result<JoinOutcome>> paced;
    REQUIRE(rig.join.join(request(1, JoinConfirmation::AlreadyConfirmed), op, std::nullopt, paced.callback()));
    rig.rt.run_until_idle();
    rig.answer_resolve(edge, entry(1, 1, "Busy"));
    REQUIRE(paced.calls == 1);
    CHECK(paced.result->error().id == "browser.too_many_join_attempts");
    CHECK(edge.idle());

    Capture<Result<JoinOutcome>> limited;
    REQUIRE(rig.join.join(request(2, JoinConfirmation::AlreadyConfirmed), op, std::nullopt, limited.callback()));
    rig.rt.run_until_idle();
    rig.answer_resolve(edge, entry(2, 2, "Other"));
    const wire::Join join = edge.expect<wire::Join>();
    edge.send(wire::Error{join.req_id, wire::ErrorCode::rate_limited, {}, 30'000});
    rig.rt.run_until_idle();
    const Diagnostic& error = limited.result->error();
    CHECK(error.id == "browser.too_many_join_attempts");
    CHECK(std::get<std::chrono::milliseconds>(*error.find_arg("retry_after")) == 30s);
}

TEST_CASE("cancelling the op while ConfirmJoin waits ends the join once", "[browser][join][race]") {
    JoinRig rig;
    BrowserLease lease = rig.session.acquire();
    Edge& edge = rig.connect();
    auto [handle, op] = rig.rt.ops().create<int>(OpKind::Play, DisconnectPolicy::Detached, std::nullopt);
    Capture<Result<JoinOutcome>> done;
    REQUIRE(rig.join.join(request(1), op, std::nullopt, done.callback()));
    rig.rt.run_until_idle();
    rig.answer_resolve(edge, entry(1, 1, "Server"));
    const auto confirm = rig.prompt(UserRequestKind::ConfirmJoin);
    REQUIRE(confirm);

    REQUIRE(rig.rt.ops().cancel(handle.id(), CancelReason::User));
    REQUIRE(done.calls == 1);
    CHECK(done.result->error().kind == ErrorKind::Cancelled);
    CHECK(rig.rt.requests().pending().empty());
    CHECK(rig.rt.requests().respond(confirm->id, ConfirmJoinAnswer{true}).error().id == "requests.already_resolved");
    rig.rt.run_until_idle();
    CHECK(done.calls == 1);
    CHECK(edge.idle());
    op.complete(Cancelled{CancelReason::User});
}

TEST_CASE("start_join is an op that completes with the outcome", "[browser][join]") {
    JoinRig rig;
    BrowserLease lease = rig.session.acquire();
    Edge& edge = rig.connect();
    const Result<OpHandle> handle = rig.join.start_join(request(1, JoinConfirmation::AlreadyConfirmed), DisconnectPolicy::Detached);
    REQUIRE(handle);
    rig.rt.run_until_idle();
    rig.answer_resolve(edge, entry(1, 1, "Server"));
    const wire::Join join = edge.expect<wire::Join>();
    edge.send(wire::JoinGrant{join.req_id, {192, 168, 1, 20}, 7777, {}, 0});
    rig.rt.run_until_idle();
    const auto outcome = rig.rt.ops().outcome(handle->id());
    REQUIRE(outcome);
    const auto* completed = std::get_if<Completed<std::any>>(&*outcome);
    REQUIRE(completed);
    CHECK(std::any_cast<JoinOutcome>(completed->value).endpoint == Endpoint{ipv4(192, 168, 1, 20), Port{7777}});
}
