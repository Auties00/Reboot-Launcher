#include <array>
#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "identity_file.hpp"
#include "messages.hpp"
#include "publish_test_support.hpp"
#include "reboot/browser/full_jitter_backoff.hpp"
#include "reboot/publish/field_limits.hpp"
#include "reboot/publish/host_publisher.hpp"
#include "reboot/publish/publish_state_changed.hpp"
#include "reboot/testing/event_recorder.hpp"
#include "reboot/testing/fake_quic_transport.hpp"

using namespace rb;
using namespace rb::publish;
using namespace rb::publish::test;
using namespace std::chrono_literals;

namespace {

constexpr const char* kEdgeHost = "sb.test";

[[nodiscard]] Diagnostic network_error() { return make_diag(ErrorDomain::Publish, msg::kEdgeUnavailable).build(); }

template <class T>
[[nodiscard]] std::string error_id(const Result<T>& result) {
    return result ? std::string{} : result.error().id;
}

[[nodiscard]] PublishRequest request(u8 session = 1, u8 profile = 1) {
    PublishRequest out;
    out.session = session_id(session);
    out.profile = profile_id(profile);
    out.metadata.name = "Reboot server";
    out.metadata.description = "Arena";
    out.metadata.author = "Host1";
    out.metadata.version = GameVersion{.major = 7, .minor = 40, .patch = std::nullopt};
    out.metadata.max_players = 100;
    out.listing = Listing::Listed;
    out.game_port = Port{7777};
    out.players = 2;
    return out;
}

struct PublisherRig : StoreRig {
    explicit PublisherRig(bool memory_only = false) {
        if (memory_only) store.load_memory_only();
        else REQUIRE(store.load({}));
    }

    // Accepts the newest connection and answers its Hello; returns the edge with HostRegister next.
    Edge& connect(u32 update_per_sec = 0) {
        strand.run_ready();
        testing::FakeQuicPeer* peer = quic.last();
        REQUIRE(peer != nullptr);
        peer->accept();
        strand.run_ready();
        edges.push_back(std::make_unique<Edge>(*peer));
        Edge& edge = *edges.back();
        const wire::Hello hello = edge.expect<wire::Hello>();
        CHECK(hello.role == wire::Role::host);
        CHECK(hello.features == wire::feature::datagrams);
        edge.welcome(3000, update_per_sec);
        strand.run_ready();
        return edge;
    }

    // Publishes `req` and runs it to Registered; the edge hands out `token` when given one.
    Edge& registered(PublishRequest req = request(), std::optional<std::array<u8, kHostTokenSize>> token = token_bytes(1),
                     u32 update_per_sec = 0) {
        const SessionId session = req.session;
        REQUIRE(publisher.publish(std::move(req)));
        Edge& edge = connect(update_per_sec);
        const wire::HostRegister reg = edge.expect<wire::HostRegister>();
        edge.registered(reg.req_id, token);
        strand.run_ready();
        REQUIRE(phase(session) == PublishPhase::Registered);
        return edge;
    }

    [[nodiscard]] std::optional<PublishPhase> phase(const SessionId& session) const {
        const auto state = publisher.state(session);
        if (!state) return std::nullopt;
        return state->phase;
    }

    [[nodiscard]] std::vector<ReachabilityChanged> reach_events() {
        recorder.pump();
        std::vector<ReachabilityChanged> out;
        for (const auto* event : recorder.payloads<ReachabilityChanged>(EventKind::ReachabilityChanged)) out.push_back(*event);
        return out;
    }

    testing::FakeQuicTransport quic{strand};
    RecordingNotices notices;
    testing::EventRecorder recorder{events};
    HostPublisher publisher{quic,   store,  notices, strand, timers, random, events,
                            browser::RbsbEndpoint{kEdgeHost, Port{443}, std::nullopt, browser::EndpointSource::Compiled}};
    std::vector<std::unique_ptr<Edge>> edges;
};

}  // namespace

TEST_CASE("publish registers the entry over an IPv4 rbsb/1 host connection", "[publish][publisher]") {
    PublisherRig rig;
    REQUIRE(rig.publisher.publish(request()));
    CHECK(rig.phase(session_id(1)) == PublishPhase::Connecting);
    CHECK(rig.publisher.has_publications());

    rig.strand.run_ready();
    const ports::QuicConnectOptions& options = rig.quic.last()->options();
    CHECK(options.host == kEdgeHost);
    CHECK(options.port == Port{443});
    CHECK(options.alpn == "rbsb/1");
    CHECK(options.ipv4_only);

    Edge& edge = rig.connect();
    const wire::HostRegister reg = edge.expect<wire::HostRegister>();
    CHECK(rig.phase(session_id(1)) == PublishPhase::Registering);
    CHECK(reg.id == rig.store.server_id(profile_id(1))->value);
    CHECK_FALSE(reg.token);
    CHECK(reg.name == "Reboot server");
    CHECK(reg.description == "Arena");
    CHECK(reg.author == "Host1");
    CHECK(reg.version == "7.40");
    CHECK(reg.game_port == 7777);
    CHECK_FALSE(reg.password);
    CHECK(reg.max_players == 100);
    CHECK(reg.players == 2);
    CHECK_FALSE(reg.hidden);

    const auto early = rig.publisher.public_endpoint(session_id(1));
    REQUIRE_FALSE(early);
    CHECK(early.error().is(msg::kNotRegistered));

    edge.registered(reg.req_id, token_bytes(3));
    rig.strand.run_ready();
    const auto state = rig.publisher.state(session_id(1));
    REQUIRE(state);
    CHECK(state->phase == PublishPhase::Registered);
    CHECK_FALSE(state->hidden);
    CHECK(state->advertised_port == Port{7777});
    CHECK_FALSE(state->error);

    const Endpoint expected{IpAddress::v4((203u << 24) | (113u << 8) | 7u), Port{7777}};
    const auto endpoint = rig.publisher.public_endpoint(session_id(1));
    REQUIRE(endpoint);
    CHECK(*endpoint == expected);
    const auto reach = rig.reach_events();
    REQUIRE_FALSE(reach.empty());
    CHECK(reach.back().status == HostStatus::AwaitingProbe);
    CHECK(reach.back().public_endpoint == expected);

    const auto link = rig.publisher.share_link(session_id(1));
    REQUIRE(link);
    CHECK(link->url() == "reboot://" + format_uuid(rig.store.server_id(profile_id(1))->value));

    // The token from HostRegistered reaches the profile's file.
    REQUIRE(rig.settle());
    const auto on_disk = decode_identity(*rig.fs.contents(identity_file(profile_id(1))));
    REQUIRE(on_disk);
    REQUIRE(on_disk->token);
    CHECK(on_disk->token->reveal() == token_bytes(3));
    CHECK(rig.recorder.count(EventKind::PublishStateChanged) > 0);
}

TEST_CASE("publish validates before anything is sent", "[publish][publisher]") {
    PublisherRig rig;
    PublishRequest no_port = request();
    no_port.game_port = Port{0};
    const auto missing = rig.publisher.publish(std::move(no_port));
    REQUIRE_FALSE(missing);
    CHECK(missing.error().is(msg::kGamePortMissing));
    CHECK_FALSE(rig.publisher.has_publications());

    REQUIRE(rig.publisher.publish(request(1, 1)));
    const auto twice = rig.publisher.publish(request(1, 2));
    REQUIRE_FALSE(twice);
    CHECK(twice.error().is(msg::kAlreadyPublished));
    const auto busy = rig.publisher.publish(request(2, 1));
    REQUIRE_FALSE(busy);
    CHECK(busy.error().is(msg::kProfileBusy));

    const SessionId unknown = session_id(9);
    CHECK(error_id(rig.publisher.update(unknown, {})) == msg::kNotPublished.id);
    CHECK(error_id(rig.publisher.set_game_port(unknown, Port{7777})) == msg::kNotPublished.id);
    CHECK(error_id(rig.publisher.set_restarting(unknown, true)) == msg::kNotPublished.id);
    CHECK(error_id(rig.publisher.set_players(unknown, 1)) == msg::kNotPublished.id);
    CHECK(error_id(rig.publisher.share_link(unknown)) == msg::kNotPublished.id);
    CHECK(error_id(rig.publisher.public_endpoint(unknown)) == msg::kNotPublished.id);
    CHECK(error_id(rig.publisher.set_players(session_id(1), kMaxPlayerLimit + 1)) == msg::kPlayerCountTooHigh.id);
    CHECK(error_id(rig.publisher.set_game_port(session_id(1), Port{0})) == msg::kGamePortMissing.id);
    MetadataPatch empty_name;
    empty_name.name = std::string();
    CHECK(error_id(rig.publisher.update(session_id(1), std::move(empty_name))) == msg::kServerNameEmpty.id);
}

TEST_CASE("a registered entry heartbeats over datagrams every heartbeat_ms / 2", "[publish][publisher]") {
    PublisherRig rig;
    Edge& edge = rig.registered();
    CHECK(edge.peer().datagrams().empty());
    rig.strand.advance(1500ms);
    REQUIRE(edge.peer().datagrams().size() == 1);
    rig.strand.advance(1500ms);
    REQUIRE(edge.peer().datagrams().size() == 2);
    wire::HostHeartbeat beat;
    REQUIRE(wire::for_each_frame(edge.peer().datagrams().back(), 64, [&](const wire::FrameView& frame) {
        return wire::decode_frame(frame, beat);
    }));
    CHECK(beat.seq == 1);
}

TEST_CASE("metadata edits are debounced into one HostUpdate with one in flight", "[publish][publisher]") {
    PublisherRig rig;
    Edge& edge = rig.registered();
    const SessionId session = session_id(1);

    MetadataPatch name;
    name.name = "Renamed";
    REQUIRE(rig.publisher.update(session, std::move(name)));
    rig.strand.advance(200ms);
    CHECK(edge.idle());
    MetadataPatch description;
    description.description = "New arena";
    description.name = "Renamed";
    REQUIRE(rig.publisher.update(session, std::move(description)));
    rig.strand.advance(200ms);
    const wire::HostUpdate first = edge.expect<wire::HostUpdate>();
    CHECK(first.req_id != 0);
    CHECK(first.name == "Renamed");
    CHECK(first.description == "New arena");
    CHECK_FALSE(first.max_players);
    CHECK_FALSE(first.hidden);
    CHECK_FALSE(first.password);
    CHECK_FALSE(first.players);

    MetadataPatch players;
    players.max_players = 50;
    players.password = SecretString("hunter22");
    REQUIRE(rig.publisher.update(session, std::move(players)));
    rig.strand.advance(1s);
    CHECK(edge.idle());
    edge.send(wire::Ack{first.req_id});
    rig.strand.run_ready();
    const wire::HostUpdate second = edge.expect<wire::HostUpdate>();
    CHECK(second.max_players == 50u);
    CHECK(second.password == std::optional<std::string>("hunter22"));
    CHECK_FALSE(second.name);
    edge.send(wire::Ack{second.req_id});

    // An empty password removes it.
    MetadataPatch clear;
    clear.password = SecretString("");
    REQUIRE(rig.publisher.update(session, std::move(clear)));
    rig.strand.advance(kUpdateDebounce);
    CHECK(edge.expect<wire::HostUpdate>().password == std::optional<std::string>(""));
}

TEST_CASE("listing, restarting and game port changes skip the debounce", "[publish][publisher]") {
    PublisherRig rig;
    Edge& edge = rig.registered();
    const SessionId session = session_id(1);

    MetadataPatch unlist;
    unlist.listing = Listing::Unlisted;
    REQUIRE(rig.publisher.update(session, std::move(unlist)));
    const wire::HostUpdate hidden = edge.expect<wire::HostUpdate>();
    CHECK(hidden.hidden == std::optional<bool>(true));
    CHECK(rig.publisher.state(session)->hidden);
    edge.send(wire::Ack{hidden.req_id});
    rig.strand.run_ready();

    MetadataPatch relist;
    relist.listing = Listing::Listed;
    REQUIRE(rig.publisher.update(session, std::move(relist)));
    edge.send(wire::Ack{edge.expect<wire::HostUpdate>().req_id});
    rig.strand.run_ready();

    // Restarting hides a listed entry, and leaving it lists it again.
    REQUIRE(rig.publisher.set_restarting(session, true));
    const wire::HostUpdate restarting = edge.expect<wire::HostUpdate>();
    CHECK(restarting.hidden == std::optional<bool>(true));
    edge.send(wire::Ack{restarting.req_id});
    rig.strand.run_ready();
    REQUIRE(rig.publisher.set_restarting(session, false));
    const wire::HostUpdate live = edge.expect<wire::HostUpdate>();
    CHECK(live.hidden == std::optional<bool>(false));
    CHECK_FALSE(rig.publisher.state(session)->hidden);
    edge.send(wire::Ack{live.req_id});
    rig.strand.run_ready();

    REQUIRE(rig.publisher.set_game_port(session, Port{7790}));
    const wire::HostUpdate port = edge.expect<wire::HostUpdate>();
    CHECK(port.game_port == 7790u);
    CHECK(rig.publisher.state(session)->advertised_port == Port{7790});
    CHECK(rig.publisher.public_endpoint(session)->port == Port{7790});
    CHECK(rig.reach_events().back().public_endpoint->port == Port{7790});
}

TEST_CASE("player counts go at once without an Ack", "[publish][publisher]") {
    PublisherRig rig;
    Edge& edge = rig.registered();
    REQUIRE(rig.publisher.set_players(session_id(1), 5));
    const wire::HostUpdate update = edge.expect<wire::HostUpdate>();
    CHECK(update.req_id == 0);
    CHECK(update.players == 5u);
    CHECK_FALSE(update.name);
    REQUIRE(rig.publisher.set_players(session_id(1), 5));
    CHECK(edge.idle());
}

TEST_CASE("RATE_LIMITED holds the update until retry_after", "[publish][publisher]") {
    PublisherRig rig;
    Edge& edge = rig.registered();
    MetadataPatch name;
    name.name = "Once";
    REQUIRE(rig.publisher.update(session_id(1), std::move(name)));
    rig.strand.advance(kUpdateDebounce);
    const wire::HostUpdate first = edge.expect<wire::HostUpdate>();
    edge.error(first.req_id, wire::ErrorCode::rate_limited, "too many updates", 1500);
    rig.strand.advance(1000ms);
    CHECK(edge.idle());
    rig.strand.advance(500ms);
    const wire::HostUpdate again = edge.expect<wire::HostUpdate>();
    CHECK(again.name == "Once");
    CHECK(again.req_id != first.req_id);
    CHECK(rig.phase(session_id(1)) == PublishPhase::Registered);
}

TEST_CASE("updates are paced within Welcome.limits", "[publish][publisher]") {
    PublisherRig rig;
    Edge& edge = rig.registered(request(), token_bytes(1), 2);
    MetadataPatch first_edit;
    first_edit.name = "One";
    REQUIRE(rig.publisher.update(session_id(1), std::move(first_edit)));
    rig.strand.advance(kUpdateDebounce);
    edge.send(wire::Ack{edge.expect<wire::HostUpdate>().req_id});
    rig.strand.run_ready();

    // Two per second: the next one waits 500 ms after the last.
    REQUIRE(rig.publisher.set_restarting(session_id(1), true));
    CHECK(edge.idle());
    rig.strand.advance(400ms);
    CHECK(edge.idle());
    rig.strand.advance(100ms);
    CHECK(edge.expect<wire::HostUpdate>().hidden == std::optional<bool>(true));
}

TEST_CASE("HostStatus drives the reachability verdict", "[publish][publisher]") {
    PublisherRig rig;
    Edge& edge = rig.registered();
    edge.send(wire::HostStatus{true, 0});
    rig.strand.run_ready();
    CHECK(rig.publisher.reachability(session_id(1))->status == HostStatus::Live);
    edge.send(wire::HostStatus{false, 2});
    rig.strand.run_ready();
    const auto reach = rig.publisher.reachability(session_id(1));
    CHECK(reach->status == HostStatus::LiveUnreachable);
    CHECK(reach->probe_failures == 2);
    CHECK(rig.reach_events().back().status == HostStatus::LiveUnreachable);
}

TEST_CASE("a lost connection reconnects and registers again with the token", "[publish][publisher]") {
    PublisherRig rig;
    Edge& edge = rig.registered(request(), token_bytes(4));
    const ServerId server = *rig.store.server_id(profile_id(1));
    edge.peer().close(network_error());
    rig.strand.run_ready();

    const auto state = rig.publisher.state(session_id(1));
    CHECK(state->phase == PublishPhase::Retrying);
    REQUIRE(state->error);
    CHECK(state->error->is(msg::kConnectionLost));
    const auto reach = rig.reach_events();
    CHECK_FALSE(reach.back().status);
    CHECK_FALSE(reach.back().public_endpoint);
    CHECK_FALSE(rig.publisher.public_endpoint(session_id(1)));

    // The session keeps edits made while it is not listed.
    REQUIRE(rig.publisher.set_players(session_id(1), 7));
    // The first backoff delay is at most FullJitterBackoff::kBase.
    rig.strand.advance(browser::FullJitterBackoff::kBase);
    REQUIRE(rig.quic.connections().size() == 2);
    Edge& second = rig.connect();
    const wire::HostRegister reg = second.expect<wire::HostRegister>();
    CHECK(reg.id == server.value);
    REQUIRE(reg.token);
    CHECK(*reg.token == token_bytes(4));
    CHECK(reg.players == 7);
    second.registered(reg.req_id);
    rig.strand.run_ready();
    CHECK(rig.phase(session_id(1)) == PublishPhase::Registered);
}

TEST_CASE("UNAUTHORIZED on our HostRegister rotates the id once and tells the user", "[publish][publisher]") {
    PublisherRig rig;
    REQUIRE(rig.publisher.publish(request()));
    Edge& edge = rig.connect();
    const wire::HostRegister first = edge.expect<wire::HostRegister>();
    const ServerId old_server{first.id};
    edge.error(first.req_id, wire::ErrorCode::unauthorized, "token required for this id");
    rig.strand.run_ready();

    REQUIRE(rig.notices.count(PublishNoticeKind::IdentityRotated) == 1);
    const PublishNotice& notice = rig.notices.received.front();
    CHECK(notice.session == session_id(1));
    CHECK(notice.profile == profile_id(1));
    CHECK(notice.message.is(msg::kIdentityRotated));
    CHECK(notice.message.severity == Severity::Warning);

    const wire::HostRegister second = edge.expect<wire::HostRegister>();
    CHECK(second.id != first.id);
    CHECK_FALSE(second.token);
    const ServerId new_server{second.id};
    CHECK(rig.store.server_id(profile_id(1)) == new_server);
    CHECK(rig.publisher.state(session_id(1))->server == new_server);
    CHECK(rig.publisher.share_link(session_id(1))->server == new_server);
    CHECK(new_server != old_server);

    // A second UNAUTHORIZED on the same connection is not about ownership.
    edge.error(second.req_id, wire::ErrorCode::unauthorized, "hello as host first");
    rig.strand.run_ready();
    const auto state = rig.publisher.state(session_id(1));
    CHECK(state->phase == PublishPhase::Refused);
    CHECK(state->error->is(msg::kEdgeNotAllowed));
    CHECK(rig.notices.count(PublishNoticeKind::IdentityRotated) == 1);
}

TEST_CASE("UNAUTHORIZED outside our HostRegister refuses and keeps the id", "[publish][publisher]") {
    PublisherRig rig;
    Edge& edge = rig.registered();
    const ServerId server = *rig.store.server_id(profile_id(1));
    MetadataPatch name;
    name.name = "Edit";
    REQUIRE(rig.publisher.update(session_id(1), std::move(name)));
    rig.strand.advance(kUpdateDebounce);
    edge.error(edge.expect<wire::HostUpdate>().req_id, wire::ErrorCode::unauthorized, "not the owner of this entry");
    rig.strand.run_ready();
    const auto state = rig.publisher.state(session_id(1));
    CHECK(state->phase == PublishPhase::Refused);
    CHECK(state->error->is(msg::kEdgeNotAllowed));
    CHECK(rig.store.server_id(profile_id(1)) == server);
    CHECK(edge.peer().closed_with());
    CHECK(rig.notices.received.empty());
}

TEST_CASE("CONFLICT on the registered connection means the id moved elsewhere", "[publish][publisher]") {
    PublisherRig rig;
    Edge& edge = rig.registered();
    edge.error(0, wire::ErrorCode::conflict, "entry taken over by another connection");
    rig.strand.run_ready();
    const auto state = rig.publisher.state(session_id(1));
    CHECK(state->phase == PublishPhase::Superseded);
    CHECK(state->error->is(msg::kHostedElsewhere));
    REQUIRE(rig.notices.count(PublishNoticeKind::HostedElsewhere) == 1);
    CHECK(edge.peer().closed_with());
    CHECK_FALSE(rig.reach_events().back().status);

    // Nothing reconnects on its own.
    rig.strand.advance(60s);
    CHECK(rig.quic.connections().size() == 1);
    CHECK(rig.phase(session_id(1)) == PublishPhase::Superseded);
}

TEST_CASE("CONFLICT answering our HostRegister retries after retry_after_ms", "[publish][publisher]") {
    PublisherRig rig;
    REQUIRE(rig.publisher.publish(request()));
    Edge& edge = rig.connect();
    const wire::HostRegister first = edge.expect<wire::HostRegister>();
    edge.error(first.req_id, wire::ErrorCode::conflict, "concurrent update, retry", 250);
    rig.strand.advance(200ms);
    CHECK(edge.idle());
    rig.strand.advance(50ms);
    const wire::HostRegister again = edge.expect<wire::HostRegister>();
    CHECK(again.id == first.id);
    edge.registered(again.req_id);
    rig.strand.run_ready();
    CHECK(rig.phase(session_id(1)) == PublishPhase::Registered);
}

TEST_CASE("edge refusals map to a known field or limit", "[publish][publisher]") {
    PublisherRig rig;
    REQUIRE(rig.publisher.publish(request(1, 1)));
    Edge& edge = rig.connect();
    edge.error(edge.expect<wire::HostRegister>().req_id, wire::ErrorCode::bad_request, "invalid name");
    rig.strand.run_ready();
    auto state = rig.publisher.state(session_id(1));
    CHECK(state->phase == PublishPhase::Refused);
    REQUIRE(state->error);
    CHECK(state->error->is(msg::kEdgeRejected));
    const Arg* field = state->error->find_arg("field");
    REQUIRE(field != nullptr);
    CHECK(std::get<std::string>(*field) == "name");

    REQUIRE(rig.publisher.publish(request(2, 2)));
    Edge& other = rig.connect();
    other.error(other.expect<wire::HostRegister>().req_id, wire::ErrorCode::limit_exceeded, "too many servers");
    rig.strand.run_ready();
    state = rig.publisher.state(session_id(2));
    CHECK(state->phase == PublishPhase::Refused);
    CHECK(state->error->is(msg::kEdgeHostLimit));
}

TEST_CASE("RATE_LIMITED on HostRegister waits and registers again", "[publish][publisher]") {
    PublisherRig rig;
    REQUIRE(rig.publisher.publish(request()));
    Edge& edge = rig.connect();
    edge.error(edge.expect<wire::HostRegister>().req_id, wire::ErrorCode::rate_limited, "slow down", 800);
    rig.strand.run_ready();
    const auto state = rig.publisher.state(session_id(1));
    CHECK(state->phase == PublishPhase::Retrying);
    CHECK(state->error->is(msg::kEdgeRateLimited));
    rig.strand.advance(800ms);
    CHECK(edge.expect<wire::HostRegister>().req_id != 0);
    CHECK(rig.phase(session_id(1)) == PublishPhase::Registering);
}

TEST_CASE("an unreachable edge leaves the session not listed and retrying", "[publish][publisher]") {
    PublisherRig rig;
    rig.quic.fail_next_open(network_error());
    REQUIRE(rig.publisher.publish(request()));
    auto state = rig.publisher.state(session_id(1));
    CHECK(state->phase == PublishPhase::Retrying);
    REQUIRE(state->error);
    CHECK(state->error->is(msg::kEdgeUnreachable));
    CHECK(rig.quic.connections().empty());

    rig.strand.advance(browser::FullJitterBackoff::kBase);
    REQUIRE(rig.quic.connections().size() == 1);
    CHECK(rig.phase(session_id(1)) == PublishPhase::Connecting);

    // No handshake within the QuicConnect deadline.
    rig.strand.advance(default_deadline(OpKind::QuicConnect));
    state = rig.publisher.state(session_id(1));
    CHECK(state->phase == PublishPhase::Retrying);
    CHECK(state->error->is(msg::kEdgeUnreachable));
    CHECK(rig.quic.connections().front()->closed_with());
}

TEST_CASE("GoAway moves the publication to a fresh connection", "[publish][publisher]") {
    PublisherRig rig;
    Edge& edge = rig.registered(request(), token_bytes(8));
    edge.send(wire::GoAway{wire::GoAwayReason::rebalance, 500});
    rig.strand.run_ready();
    const auto state = rig.publisher.state(session_id(1));
    CHECK(state->phase == PublishPhase::Retrying);
    CHECK(state->error->is(msg::kEdgeGoAway));
    CHECK_FALSE(edge.peer().closed_with());

    rig.strand.advance(browser::FullJitterBackoff::kBase);
    REQUIRE(rig.quic.connections().size() == 2);
    CHECK(edge.peer().closed_with());
    Edge& second = rig.connect();
    const wire::HostRegister reg = second.expect<wire::HostRegister>();
    REQUIRE(reg.token);
    CHECK(*reg.token == token_bytes(8));
    second.registered(reg.req_id);
    rig.strand.run_ready();
    CHECK(rig.phase(session_id(1)) == PublishPhase::Registered);
}

TEST_CASE("a CONFLICT left on a connection being replaced never stops the publication", "[publish][publisher]") {
    PublisherRig rig;
    Edge& edge = rig.registered();
    edge.send(wire::GoAway{wire::GoAwayReason::shutdown, 0});
    rig.strand.run_ready();
    edge.error(0, wire::ErrorCode::conflict, "entry taken over by another connection");
    rig.strand.run_ready();
    CHECK(rig.phase(session_id(1)) == PublishPhase::Retrying);
    CHECK(rig.notices.received.empty());
    rig.strand.advance(browser::FullJitterBackoff::kBase);
    Edge& second = rig.connect();
    second.registered(second.expect<wire::HostRegister>().req_id);
    rig.strand.run_ready();
    CHECK(rig.phase(session_id(1)) == PublishPhase::Registered);
}

TEST_CASE("withdraw unregisters, waits for the Ack and frees the identity", "[publish][publisher]") {
    PublisherRig rig;
    Edge& edge = rig.registered();
    bool done = false;
    rig.publisher.withdraw(session_id(1), [&] { done = true; });
    CHECK(rig.phase(session_id(1)) == PublishPhase::Withdrawing);
    const wire::HostUnregister unregister = edge.expect<wire::HostUnregister>();
    CHECK_FALSE(done);
    CHECK_FALSE(rig.store.acquire(profile_id(1)));
    CHECK(error_id(rig.publisher.update(session_id(1), {})) == msg::kNotPublished.id);

    edge.send(wire::Ack{unregister.req_id});
    rig.strand.run_ready();
    CHECK(done);
    CHECK_FALSE(rig.publisher.state(session_id(1)));
    CHECK_FALSE(rig.publisher.has_publications());
    CHECK(edge.peer().closed_with());
    CHECK_FALSE(rig.reach_events().back().status);
    CHECK(rig.store.acquire(profile_id(1)));
}

TEST_CASE("withdraw closes anyway once the Ack is overdue", "[publish][publisher]") {
    PublisherRig rig;
    Edge& edge = rig.registered();
    int done = 0;
    rig.publisher.withdraw(session_id(1), [&] { ++done; });
    rig.publisher.withdraw(session_id(1), [&] { ++done; });
    (void)edge.expect<wire::HostUnregister>();
    CHECK(edge.idle());
    rig.strand.advance(kUnregisterAckWait - 1ms);
    CHECK(done == 0);
    rig.strand.advance(1ms);
    CHECK(done == 2);
    CHECK(edge.peer().closed_with());
}

TEST_CASE("withdraw of an unregistered or unknown session completes at once", "[publish][publisher]") {
    PublisherRig rig;
    bool unknown = false;
    rig.publisher.withdraw(session_id(5), [&] { unknown = true; });
    CHECK(unknown);

    rig.quic.fail_next_open(network_error());
    REQUIRE(rig.publisher.publish(request()));
    bool retrying = false;
    rig.publisher.withdraw(session_id(1), [&] { retrying = true; });
    CHECK(retrying);
    rig.strand.advance(10s);
    CHECK(rig.quic.connections().empty());
}

TEST_CASE("withdraw_all waits for every publication", "[publish][publisher]") {
    PublisherRig rig;
    bool none = false;
    rig.publisher.withdraw_all([&] { none = true; });
    CHECK(none);

    Edge& first = rig.registered(request(1, 1));
    Edge& second = rig.registered(request(2, 2));
    bool done = false;
    rig.publisher.withdraw_all([&] { done = true; });
    first.send(wire::Ack{first.expect<wire::HostUnregister>().req_id});
    rig.strand.run_ready();
    CHECK_FALSE(done);
    second.send(wire::Ack{second.expect<wire::HostUnregister>().req_id});
    rig.strand.run_ready();
    CHECK(done);
    CHECK_FALSE(rig.publisher.has_publications());
}

TEST_CASE("a token that cannot be saved is noticed", "[publish][publisher]") {
    PublisherRig rig(true);
    (void)rig.registered(request(), token_bytes(6));
    rig.strand.run_until([&] { return rig.notices.count(PublishNoticeKind::TokenNotSaved) == 1; });
    const PublishNotice& notice = rig.notices.received.back();
    CHECK(notice.message.is(msg::kTokenNotSaved));
    CHECK(notice.message.severity == Severity::Warning);
    CHECK(rig.phase(session_id(1)) == PublishPhase::Registered);
}

TEST_CASE("set_edge applies from the next connection on", "[publish][publisher]") {
    PublisherRig rig;
    Edge& edge = rig.registered();
    rig.publisher.set_edge(browser::RbsbEndpoint{"edge2.test", Port{8443}, std::nullopt, browser::EndpointSource::Manifest});
    CHECK_FALSE(edge.peer().closed_with());
    edge.peer().close(std::nullopt);
    rig.strand.advance(browser::FullJitterBackoff::kBase);
    REQUIRE(rig.quic.connections().size() == 2);
    CHECK(rig.quic.last()->options().host == "edge2.test");
    CHECK(rig.quic.last()->options().port == Port{8443});
}
