#include <catch2/catch_test_macros.hpp>
#include <string>
#include <vector>

#include "browser_test_support.hpp"
#include "reboot/browser/connection_state.hpp"
#include "reboot/foundation/cancel.hpp"
#include "reboot/foundation/events.hpp"

using namespace rb;
using namespace rb::browser;
using namespace rb::browser::test;
using namespace std::chrono_literals;

namespace {

[[nodiscard]] std::vector<ConnectionState> states(const testing::EventRecorder& recorder) {
    std::vector<ConnectionState> out;
    for (const ConnectionStatus* status : recorder.payloads<ConnectionStatus>(EventKind::BrowserConnectionChanged))
        out.push_back(status->state);
    return out;
}

}  // namespace

TEST_CASE("a lease connects, says Hello and goes idle when released", "[browser][session]") {
    SessionRig rig;
    testing::EventRecorder recorder(rig.rt.events());
    CHECK(rig.session.status().state == ConnectionState::Idle);

    BrowserLease lease = rig.session.acquire();
    CHECK(rig.session.status().state == ConnectionState::Connecting);
    // Status events coalesce to the newest, so each step is drained before the next.
    std::vector<ConnectionState> seen;
    const auto drain = [&] {
        recorder.pump();
        for (const ConnectionState state : states(recorder)) seen.push_back(state);
        recorder.clear();
    };
    drain();
    Edge& edge = rig.connect();
    drain();

    const ports::QuicConnectOptions& options = edge.peer().options();
    CHECK(options.host == kEdgeHost);
    CHECK(options.alpn == "rbsb/1");
    CHECK(options.remote == ipv4(10, 0, 0, 1));
    CHECK(options.keepalive == 20s);
    REQUIRE(rig.session.edge());
    CHECK(rig.session.edge()->edge_id == 42);
    CHECK(rig.session.edge()->clock_offset == std::chrono::milliseconds{kEdgeTimeMs});

    lease.release();
    CHECK_FALSE(lease.held());
    CHECK(rig.session.status().state == ConnectionState::Idle);
    CHECK(edge.peer().closed_with().has_value());
    CHECK_FALSE(rig.session.edge());
    drain();
    CHECK(seen == std::vector{ConnectionState::Connecting, ConnectionState::Connected, ConnectionState::Idle});
}

TEST_CASE("every address of the edge is tried in turn", "[browser][session]") {
    SessionRig rig;
    rig.dns.set(kEdgeHost, {ipv4(10, 0, 0, 1), ipv4(10, 0, 0, 2)});
    BrowserLease lease = rig.session.acquire();
    rig.rt.run_until_idle();
    REQUIRE(rig.quic.connections().size() == 1);
    rig.quic.last()->refuse(make_diag(ErrorDomain::Net, MessageId{"net.quic_connect_failed"}).build());
    rig.rt.run_until_idle();
    REQUIRE(rig.quic.connections().size() == 2);
    CHECK(rig.quic.last()->options().remote == ipv4(10, 0, 0, 2));
    Edge& edge = rig.connect();
    CHECK(&edge.peer() == rig.quic.connections()[1]);
}

TEST_CASE("a request issued while disconnected waits for Welcome", "[browser][session]") {
    SessionRig rig;
    Capture<RbsbResult<wire::ResolveResult>> done;
    rig.session.resolve(server_id(1), {}, done.callback());
    Edge& edge = rig.connect();

    const wire::Resolve resolve = edge.expect<wire::Resolve>();
    CHECK(resolve.id == server_id(1).value);
    wire::ResolveResult answer;
    answer.req_id = resolve.req_id;
    answer.details = wire::EntryDetails{entry(1, 1, "One"), "desc", kEdgeTimeMs};
    edge.send(answer);
    rig.rt.run_until_idle();
    REQUIRE(done.calls == 1);
    REQUIRE(*done.result);
    CHECK((*done.result)->details->description == "desc");
    // The request's own lease went with it.
    CHECK(rig.session.status().state == ConnectionState::Idle);
}

TEST_CASE("a request that never sees Connected fails NotConnected with the cause", "[browser][session]") {
    SessionRig rig;
    rig.http_transport.route("GET", "https://edge.test/status", testing::FakeHttpResponse{.status = 503});
    Capture<RbsbResult<wire::QueryResult>> done;
    rig.session.query(wire::Query{}, {}, done.callback());
    rig.rt.run_until_idle();
    rig.quic.last()->refuse(make_diag(ErrorDomain::Net, MessageId{"net.quic_connect_failed"}).build());
    rig.rt.run_until_idle();
    CHECK(rig.session.status().state == ConnectionState::ServiceDown);
    CHECK(rig.session.status().next_attempt.has_value());

    rig.rt.advance(15s);
    REQUIRE(done.calls == 1);
    REQUIRE_FALSE(*done.result);
    CHECK(done.result->error().failure == RbsbFailure::NotConnected);
    REQUIRE(done.result->error().cause);
    CHECK(to_diagnostic(done.result->error()).id == "browser.service_down");
}

TEST_CASE("an unanswered request times out and a late answer is dropped", "[browser][session]") {
    SessionRig rig;
    BrowserLease lease = rig.session.acquire();
    Edge& edge = rig.connect();
    Capture<RbsbResult<wire::QueryResult>> done;
    rig.session.query(wire::Query{}, {}, done.callback());
    const wire::Query query = edge.expect<wire::Query>();
    rig.rt.advance(10s);
    REQUIRE(done.calls == 1);
    CHECK(done.result->error().failure == RbsbFailure::TimedOut);

    edge.send(wire::QueryResult{query.req_id, {}, {}, 0});
    rig.rt.run_until_idle();
    CHECK(done.calls == 1);
}

TEST_CASE("an edge Error answers the request it names", "[browser][session]") {
    SessionRig rig;
    BrowserLease lease = rig.session.acquire();
    Edge& edge = rig.connect();
    Capture<RbsbResult<wire::QueryResult>> done;
    rig.session.query(wire::Query{}, {}, done.callback());
    const wire::Query query = edge.expect<wire::Query>();
    edge.send(wire::Error{query.req_id, wire::ErrorCode::rate_limited, "slow down", 1500});
    rig.rt.run_until_idle();
    REQUIRE(done.calls == 1);
    const RbsbRequestError& error = done.result->error();
    CHECK(error.failure == RbsbFailure::Rejected);
    CHECK(error.code == wire::ErrorCode::rate_limited);
    CHECK(error.retry_after == 1500ms);
    CHECK(to_diagnostic(error).id == "browser.rate_limited");
}

TEST_CASE("cancel and answer race: done runs once", "[browser][session][race]") {
    for (const bool cancel_first : {true, false}) {
        SessionRig rig;
        BrowserLease lease = rig.session.acquire();
        Edge& edge = rig.connect();
        CancelSource cancel;
        Capture<RbsbResult<wire::ResolveResult>> done;
        rig.session.resolve(server_id(2), cancel.token(), done.callback());
        const wire::Resolve resolve = edge.expect<wire::Resolve>();
        if (cancel_first) cancel.cancel(CancelReason::User);
        edge.send(wire::ResolveResult{resolve.req_id, std::nullopt});
        if (!cancel_first) {
            rig.rt.run_until_idle();
            cancel.cancel(CancelReason::User);
        }
        rig.rt.run_until_idle();
        REQUIRE(done.calls == 1);
        CHECK(done.result->has_value() == !cancel_first);
    }
}

TEST_CASE("an already cancelled token fails the request without sending it", "[browser][session]") {
    SessionRig rig;
    BrowserLease lease = rig.session.acquire();
    Edge& edge = rig.connect();
    CancelSource cancel;
    cancel.cancel(CancelReason::Superseded);
    Capture<RbsbResult<wire::JoinGrant>> done;
    rig.session.join(server_id(3), std::nullopt, cancel.token(), done.callback());
    CHECK(done.calls == 0);
    rig.rt.run_until_idle();
    REQUIRE(done.calls == 1);
    CHECK(done.result->error().failure == RbsbFailure::Cancelled);
    CHECK(edge.idle());
}

TEST_CASE("the join password travels only in the Join frame", "[browser][session]") {
    SessionRig rig;
    BrowserLease lease = rig.session.acquire();
    Edge& edge = rig.connect();
    Capture<RbsbResult<wire::JoinGrant>> done;
    rig.session.join(server_id(4), SecretString(std::string("hunter2")), {}, done.callback());
    const wire::Join join = edge.expect<wire::Join>();
    CHECK(join.id == server_id(4).value);
    REQUIRE(join.password);
    CHECK(*join.password == "hunter2");
    edge.send(wire::JoinGrant{join.req_id, {1, 2, 3, 4}, 7777, {}, 0});
    rig.rt.run_until_idle();
    REQUIRE(done.calls == 1);
    CHECK((*done.result)->port == 7777);
}

TEST_CASE("a lost connection fails requests in flight, backs off and replays views", "[browser][session]") {
    SessionRig rig;
    testing::EventRecorder recorder(rig.rt.events());
    int opened = 0;
    int lost = 0;
    std::vector<u32> snapshots;
    ViewStreamCallbacks callbacks;
    callbacks.on_open = [&](const wire::SubOpen&) { ++opened; };
    callbacks.on_snapshot = [&](const wire::Snapshot& snapshot) { snapshots.push_back(snapshot.total); };
    callbacks.on_lost = [&] { ++lost; };
    auto subscription = rig.session.subscribe(wire::ViewSpec{}, 50, std::move(callbacks));
    REQUIRE(subscription);
    Edge& first = rig.connect();
    const wire::Subscribe subscribe = first.expect<wire::Subscribe>();
    first.send(wire::SubOpen{subscribe.req_id, subscribe.sub_id, 9, 50});
    first.snapshot(wire::Snapshot{9, 5, 1, {entry(1, 1, "One")}});
    rig.rt.run_until_idle();
    CHECK(opened == 1);
    CHECK(snapshots == std::vector<u32>{1});

    Capture<RbsbResult<wire::QueryResult>> done;
    rig.session.query(wire::Query{}, {}, done.callback());
    (void)first.expect<wire::Query>();
    first.peer().close(make_diag(ErrorDomain::Net, MessageId{"net.quic_connection_lost"}).build());
    rig.rt.run_until_idle();
    REQUIRE(done.calls == 1);
    CHECK(done.result->error().failure == RbsbFailure::ConnectionLost);
    CHECK(lost == 1);
    CHECK(rig.session.status().state == ConnectionState::Backoff);
    REQUIRE(rig.session.status().next_attempt);
    CHECK(*rig.session.status().next_attempt <= rig.rt.clock().steady_now() + FullJitterBackoff::kBase);
    CHECK(rig.session.status().error->id == "browser.connection_lost");

    rig.rt.advance(FullJitterBackoff::kBase);
    REQUIRE(rig.quic.connections().size() == 2);
    Edge& second = rig.connect();
    const wire::Subscribe replay = second.expect<wire::Subscribe>();
    CHECK(replay.sub_id != subscribe.sub_id);
    second.send(wire::SubOpen{replay.req_id, replay.sub_id, 11, 50});
    second.snapshot(wire::Snapshot{11, 2, 0, {}});
    rig.rt.run_until_idle();
    CHECK(opened == 2);
    CHECK(snapshots == std::vector<u32>{1, 0});
}

TEST_CASE("a snapshot that overtakes its SubOpen is held for it", "[browser][session]") {
    SessionRig rig;
    BrowserLease lease = rig.session.acquire();
    Edge& edge = rig.connect();
    std::vector<std::string> seen;
    ViewStreamCallbacks callbacks;
    callbacks.on_open = [&](const wire::SubOpen&) { seen.emplace_back("open"); };
    callbacks.on_snapshot = [&](const wire::Snapshot&) { seen.emplace_back("snapshot"); };
    callbacks.on_delta = [&](const wire::Delta&) { seen.emplace_back("delta"); };
    auto subscription = rig.session.subscribe(wire::ViewSpec{}, 50, std::move(callbacks));
    const wire::Subscribe subscribe = edge.expect<wire::Subscribe>();
    edge.snapshot(wire::Snapshot{5, 1, 0, {}});
    edge.datagram(wire::Delta{5, {}, std::nullopt});
    rig.rt.run_until_idle();
    CHECK(seen.empty());
    edge.send(wire::SubOpen{subscribe.req_id, subscribe.sub_id, 5, 50});
    rig.rt.run_until_idle();
    CHECK(seen == std::vector<std::string>{"open", "snapshot", "delta"});

    subscription->reset();
    const wire::Unsubscribe unsubscribe = edge.expect<wire::Unsubscribe>();
    CHECK(unsubscribe.sub_id == subscribe.sub_id);
}

TEST_CASE("a malformed snapshot stream is ignored until its FIN", "[browser][session]") {
    SessionRig rig;
    BrowserLease lease = rig.session.acquire();
    Edge& edge = rig.connect();
    std::vector<u64> snapshots;
    ViewStreamCallbacks callbacks;
    callbacks.on_snapshot = [&](const wire::Snapshot& snapshot) { snapshots.push_back(snapshot.vseq); };
    auto subscription = rig.session.subscribe(wire::ViewSpec{}, 50, std::move(callbacks));
    const wire::Subscribe subscribe = edge.expect<wire::Subscribe>();
    edge.send(wire::SubOpen{subscribe.req_id, subscribe.sub_id, 5, 50});
    // A Snapshot header whose length is far past the stream cap.
    edge.peer().send(7, {0x30, 0xC0, 0, 0, 1, 0, 0, 0, 0}, false);
    edge.peer().send(7, wire::frame_bytes(wire::Snapshot{5, 1, 0, {}}), true);
    rig.rt.run_until_idle();
    CHECK(snapshots.empty());

    edge.snapshot(wire::Snapshot{5, 2, 0, {}}, 7);
    rig.rt.run_until_idle();
    CHECK(snapshots == std::vector<u64>{2});
}

TEST_CASE("views beyond the edge's subscription limit fail at once", "[browser][session]") {
    SessionRig rig;
    BrowserLease lease = rig.session.acquire();
    rig.connect(1);
    auto first = rig.session.subscribe(wire::ViewSpec{}, 50, {});
    REQUIRE(first);
    auto second = rig.session.subscribe(wire::ViewSpec{}, 50, {});
    REQUIRE_FALSE(second);
    CHECK(second.error().id == "browser.too_many_views");
}

TEST_CASE("a rejected Subscribe reaches on_rejected and is not replayed", "[browser][session]") {
    SessionRig rig;
    BrowserLease lease = rig.session.acquire();
    Edge& edge = rig.connect();
    std::optional<wire::ErrorCode> rejected;
    ViewStreamCallbacks callbacks;
    callbacks.on_rejected = [&](const RbsbRequestError& error) { rejected = error.code; };
    auto subscription = rig.session.subscribe(wire::ViewSpec{}, 50, std::move(callbacks));
    const wire::Subscribe subscribe = edge.expect<wire::Subscribe>();
    edge.send(wire::Error{subscribe.req_id, wire::ErrorCode::limit_exceeded, {}, 0});
    rig.rt.run_until_idle();
    CHECK(rejected == wire::ErrorCode::limit_exceeded);
}

TEST_CASE("GoAway drains, keeps serving, then moves after the delay", "[browser][session]") {
    SessionRig rig;
    BrowserLease lease = rig.session.acquire();
    Edge& edge = rig.connect();
    edge.send(wire::GoAway{wire::GoAwayReason::rebalance, 2000});
    rig.rt.run_until_idle();
    CHECK(rig.session.status().state == ConnectionState::Draining);
    REQUIRE(rig.session.status().next_attempt);
    CHECK(*rig.session.status().next_attempt <= rig.rt.clock().steady_now() + 2s);

    Capture<RbsbResult<wire::ResolveResult>> done;
    rig.session.resolve(server_id(1), {}, done.callback());
    const wire::Resolve resolve = edge.expect<wire::Resolve>();
    edge.send(wire::ResolveResult{resolve.req_id, std::nullopt});
    rig.rt.run_until_idle();
    CHECK(done.calls == 1);

    rig.rt.advance(2s);
    CHECK(edge.peer().closed_with().has_value());
    REQUIRE(rig.quic.connections().size() == 2);
    rig.connect();
}

TEST_CASE("a failed attempt is explained by the probes", "[browser][session]") {
    SECTION("NXDOMAIN while the internet works is ServiceDown") {
        SessionRig rig;
        rig.dns.fail(kEdgeHost, make_diag(ErrorDomain::Net, MessageId{"testing.unresolved"}).kind(ErrorKind::NotFound).build());
        rig.http_transport.route("GET", "https://connectivity.test/", testing::FakeHttpResponse{.status = 204});
        BrowserLease lease = rig.session.acquire();
        rig.rt.run_until_idle();
        CHECK(rig.session.status().state == ConnectionState::ServiceDown);
        CHECK(rig.quic.connections().empty());
    }
    SECTION("a QUIC timeout while /status answers is UdpBlocked") {
        SessionRig rig;
        rig.http_transport.route("GET", "https://edge.test/status", testing::FakeHttpResponse{.status = 200});
        BrowserLease lease = rig.session.acquire();
        rig.rt.advance(10s);
        CHECK(rig.session.status().state == ConnectionState::UdpBlocked);
        CHECK(rig.session.status().error->id == "browser.udp_blocked");
        CHECK(rig.quic.last()->closed_with().has_value());
    }
    SECTION("nothing answering is Offline") {
        SessionRig rig;
        BrowserLease lease = rig.session.acquire();
        rig.rt.run_until_idle();
        rig.quic.last()->refuse(make_diag(ErrorDomain::Net, MessageId{"net.quic_udp_blocked"}).build());
        rig.rt.run_until_idle();
        CHECK(rig.session.status().state == ConnectionState::Offline);
        CHECK(rig.session.status().error->id == "browser.offline");
    }
    SECTION("a refusal the probes cannot explain is Backoff, and the session keeps retrying") {
        SessionRig rig;
        rig.http_transport.route("GET", "https://edge.test/status", testing::FakeHttpResponse{.status = 200});
        BrowserLease lease = rig.session.acquire();
        rig.rt.run_until_idle();
        rig.quic.last()->refuse(make_diag(ErrorDomain::Net, MessageId{"net.quic_connect_failed"}).build());
        rig.rt.run_until_idle();
        CHECK(rig.session.status().state == ConnectionState::Backoff);
        rig.rt.advance(FullJitterBackoff::kBase);
        CHECK(rig.quic.connections().size() == 2);
        CHECK(rig.session.status().state == ConnectionState::Connecting);
    }
}

TEST_CASE("a new endpoint moves an open connection at once", "[browser][session]") {
    SessionRig rig;
    rig.dns.set("other.test", {ipv4(10, 0, 0, 9)});
    BrowserLease lease = rig.session.acquire();
    Edge& edge = rig.connect();
    rig.session.set_endpoint(RbsbEndpoint{"other.test", Port{4433}, std::nullopt, EndpointSource::Expert});
    CHECK(edge.peer().closed_with().has_value());
    rig.rt.run_until_idle();
    REQUIRE(rig.quic.connections().size() == 2);
    CHECK(rig.quic.last()->options().host == "other.test");
    CHECK(rig.quic.last()->options().port == Port{4433});
    CHECK(rig.session.endpoint().source == EndpointSource::Expert);
}
