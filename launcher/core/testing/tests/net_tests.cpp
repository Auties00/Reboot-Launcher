#include <chrono>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <variant>
#include <vector>

#include <catch2/catch_message.hpp>
#include <catch2/catch_test_macros.hpp>

#include "reboot/foundation/cancel.hpp"
#include "reboot/ports/net.hpp"
#include "reboot/testing/deterministic_runtime.hpp"
#include "reboot/testing/fake_http_transport.hpp"
#include "reboot/testing/fake_platform_paths.hpp"
#include "reboot/testing/fake_quic_transport.hpp"
#include "reboot/testing/fake_resolver.hpp"
#include "reboot/testing/manual_waiter.hpp"
#include "reboot/testing/port_conformance.hpp"

using namespace reboot;
using namespace reboot::testing;
using namespace std::chrono_literals;

namespace {

void require_passed(const ConformanceReport& report) {
    INFO(report.describe());
    REQUIRE(report.passed());
}

struct Answer {
    std::optional<u32> status;
    std::vector<u8> body;
    int done = 0;
    std::optional<Result<ports::HttpStatus>> result;
};

[[nodiscard]] ports::HttpCallbacks into(Answer& answer) {
    ports::HttpCallbacks callbacks;
    callbacks.on_headers = [&answer](ports::HttpStatus status, const std::vector<ports::HttpHeader>&) { answer.status = status.code; };
    callbacks.on_body_chunk = [&answer](std::span<const u8> chunk) {
        answer.body.insert(answer.body.end(), chunk.begin(), chunk.end());
        return true;
    };
    callbacks.on_done = [&answer](Result<ports::HttpStatus> result) {
        ++answer.done;
        answer.result = std::move(result);
    };
    return callbacks;
}

[[nodiscard]] ports::HttpRequest get(std::string url) {
    ports::HttpRequest request;
    request.method = "GET";
    request.url = std::move(url);
    return request;
}

}  // namespace

TEST_CASE("FakeHttpTransport passes the HTTP suite", "[testing][conformance][net]") {
    DeterministicRuntime runtime;
    ManualWaiter waiter(runtime);
    FakeHttpTransport http(runtime.strand(), runtime.clock());
    const std::vector<u8> body{'r', 'e', 'b', 'o', 'o', 't', 0x00, 0xFF, '!', '?'};
    http.route("GET", "https://fake.test/ok", {200, {}, body, 3});
    FakeHttpResponse stalled;
    stalled.body = body;
    stalled.stall_after = 0;
    http.route("GET", "https://fake.test/stalled", stalled);
    FakeHttpResponse ranged;
    ranged.body = body;
    ranged.ranges = true;
    http.route("GET", "https://fake.test/range", ranged);
    require_passed(run_http_transport_conformance(
        http, {"https://fake.test/ok", body, "https://fake.test/stalled", "https://fake.test/range"},
        {waiter, default_fake_root()}));
    CHECK(http.in_flight() == 0);
}

TEST_CASE("FakeHttpTransport routes, delays and sequences answers", "[testing][net]") {
    DeterministicRuntime runtime;
    FakeHttpTransport http(runtime.strand(), runtime.clock());
    http.route("GET", "https://api.test/a", {200, {}, {'1'}});
    http.route("GET", "https://api.test/a", {201, {}, {'2'}});
    http.route_sequence("POST", "https://api.test/s", {{500, {}, {}}, {200, {}, {'k'}}});
    http.route_handler("GET", "https://api.test/h/", [](const ports::HttpRequest& request) {
        return FakeHttpResponse{200, {}, {request.url.begin(), request.url.end()}};
    });

    Answer newest;
    http.perform(get("https://api.test/a"), into(newest), {});
    Answer first_try;
    Answer second_try;
    Answer third_try;
    ports::HttpRequest post = get("https://api.test/s");
    post.method = "POST";
    http.perform(post, into(first_try), {});
    http.perform(post, into(second_try), {});
    http.perform(post, into(third_try), {});
    Answer handled;
    http.perform(get("https://api.test/h/x"), into(handled), {});
    Answer missing;
    http.perform(get("https://api.test/none"), into(missing), {});
    runtime.run_until_idle();

    CHECK(newest.status == 201u);
    CHECK(first_try.status == 500u);
    CHECK(second_try.status == 200u);
    CHECK(third_try.status == 200u);
    CHECK(std::string(handled.body.begin(), handled.body.end()) == "https://api.test/h/x");
    REQUIRE(missing.result);
    CHECK(missing.result->error().id == "testing.no_http_route");
    CHECK(http.requests().size() == 6);

    FakeHttpResponse slow{200, {}, {'z'}};
    slow.delay = 2s;
    http.route("GET", "https://api.test/slow", slow);
    Answer delayed;
    http.perform(get("https://api.test/slow"), into(delayed), {});
    runtime.advance(1s);
    CHECK(delayed.done == 0);
    CHECK(http.in_flight() == 1);
    runtime.advance(1s);
    CHECK(delayed.done == 1);

    ports::HttpRequest bounded = get("https://api.test/slow");
    bounded.connect_timeout = 500ms;
    Answer timed_out;
    http.perform(bounded, into(timed_out), {});
    runtime.advance(3s);
    REQUIRE(timed_out.done == 1);
    // The ids and arguments the real transport reports, so HttpClient tests see what production sees.
    const Diagnostic& connect_error = timed_out.result->error();
    CHECK(connect_error.id == "net.connect_timeout");
    CHECK(connect_error.retryable);
    REQUIRE(connect_error.find_arg("host") != nullptr);
    CHECK(std::get<std::string>(*connect_error.find_arg("host")) == "api.test");
    CHECK(std::get<std::chrono::milliseconds>(*connect_error.find_arg("limit")) == 500ms);

    ports::HttpRequest total = get("https://user@api.test:8443/slow?x=1");
    total.total_timeout = 1s;
    http.route("GET", "https://user@api.test:8443/slow?x=1", slow);
    Answer too_long;
    http.perform(total, into(too_long), {});
    runtime.advance(3s);
    REQUIRE(too_long.done == 1);
    CHECK(too_long.result->error().id == "net.request_timeout");
    CHECK(std::get<std::string>(*too_long.result->error().find_arg("host")) == "api.test");

    FakeHttpResponse stalls{200, {}, {'a', 'b', 'c'}};
    stalls.stall_after = 1;
    http.route("GET", "https://[::1]/stall", stalls);
    ports::HttpRequest stalled = get("https://[::1]/stall");
    stalled.stall = ports::StallPolicy{1024, 2s};
    Answer stuck;
    http.perform(stalled, into(stuck), {});
    runtime.advance(3s);
    REQUIRE(stuck.done == 1);
    CHECK(stuck.result->error().id == "net.transfer_stalled");
    CHECK(std::get<std::string>(*stuck.result->error().find_arg("host")) == "::1");

    FakeHttpResponse broken;
    broken.error = make_diag(ErrorDomain::Internal, MessageId{"internal.bug"}).build();
    http.route("GET", "https://api.test/broken", broken);
    Answer failed;
    http.perform(get("https://api.test/broken"), into(failed), {});
    runtime.run_until_idle();
    CHECK(failed.result->error().id == "internal.bug");
}

TEST_CASE("FakeResolver passes the resolver suite and answers exactly once", "[testing][conformance][net]") {
    DeterministicRuntime runtime;
    ManualWaiter waiter(runtime);
    FakeResolver resolver(runtime.strand());
    require_passed(run_resolver_conformance(resolver, {waiter, default_fake_root()}));

    resolver.set("edge.reboot.test", {IpAddress::v4(0x0A000001)});
    resolver.hang("slow.test");
    std::vector<Result<std::vector<IpAddress>>> answers;
    const auto record = [&answers](Result<std::vector<IpAddress>> result) { answers.push_back(std::move(result)); };
    resolver.resolve("EDGE.reboot.test", {}, record);
    resolver.resolve("10.1.2.3", {}, record);
    CancelSource source;
    resolver.resolve("slow.test", source.token(), record);
    runtime.run_until_idle();
    REQUIRE(answers.size() == 2);
    CHECK(answers[0]->front() == IpAddress::v4(0x0A000001));
    CHECK(answers[1]->front() == IpAddress::v4(0x0A010203));
    CHECK(resolver.pending() == 1);

    source.cancel(CancelReason::Deadline);
    runtime.run_until_idle();
    REQUIRE(answers.size() == 3);
    CHECK(answers[2].error().kind == ErrorKind::Cancelled);
    CHECK(resolver.pending() == 0);
    CHECK(resolver.queries().back() == "slow.test");
}

TEST_CASE("FakeQuicTransport passes the QUIC suite with an echoing peer", "[testing][conformance][net]") {
    DeterministicRuntime runtime;
    ManualWaiter waiter(runtime);
    FakeQuicTransport quic(runtime.strand());
    quic.on_connection([](FakeQuicPeer& peer) {
        if (peer.options().host == "refused.test") {
            peer.refuse(make_diag(ErrorDomain::Internal, MessageId{"internal.bug"}));
            return;
        }
        peer.accept();
        peer.on_stream_data([&peer](u64 stream, std::span<const u8> bytes, bool fin) {
            peer.send(stream, {bytes.begin(), bytes.end()}, fin);
        });
        peer.on_datagram([&peer](std::span<const u8> bytes) { peer.send_datagram({bytes.begin(), bytes.end()}); });
    });
    ports::QuicConnectOptions echo{"edge.test", Port{4433}, "rbsb/1", false, std::nullopt};
    ports::QuicConnectOptions refused{"refused.test", Port{4433}, "rbsb/1", false, std::nullopt};
    require_passed(run_quic_transport_conformance(quic, {echo, refused}, {waiter, default_fake_root()}));
    REQUIRE(quic.connections().size() == 2);
    FakeQuicPeer& first = *quic.connections().front();
    CHECK(first.streams() == std::vector<u64>{0});
    CHECK(first.fin_received(0));
    CHECK(first.closed_with() == 0u);
    CHECK(first.datagrams().size() == 1);
}

TEST_CASE("a QUIC peer stops reaching a client that dropped its connection", "[testing][net]") {
    DeterministicRuntime runtime;
    FakeQuicTransport quic(runtime.strand());
    int connected = 0;
    int closed = 0;
    ports::QuicCallbacks callbacks;
    callbacks.on_connected = [&connected] { ++connected; };
    callbacks.on_closed = [&closed](std::optional<Diagnostic>) { ++closed; };
    auto connection = quic.open_connection({"edge.test", Port{1}, "rbsb/1", false, std::nullopt}, std::move(callbacks));
    REQUIRE(connection);
    FakeQuicPeer* peer = quic.last();
    REQUIRE(peer != nullptr);
    CHECK((*connection)->open_stream() == 0u);
    CHECK((*connection)->open_stream() == 4u);
    CHECK_FALSE((*connection)->send(8, {1}, false));
    peer->accept();
    connection->reset();
    CHECK(peer->released());
    peer->close(std::nullopt);
    runtime.run_until_idle();
    CHECK(connected == 0);
    CHECK(closed == 0);

    quic.fail_next_open(make_diag(ErrorDomain::Internal, MessageId{"internal.bug"}));
    CHECK_FALSE(quic.open_connection({}, {}));
}
