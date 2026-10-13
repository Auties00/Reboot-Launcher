#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <deque>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include "engine_rig.hpp"
#include "reboot/api/v1/browser.hpp"
#include "reboot/browser/rbsb_endpoint.hpp"
#include "reboot/testing/fake_quic_peer.hpp"
#include "wire/frame.hpp"
#include "wire/messages.hpp"

using namespace rb;
using namespace rb::engine;
using namespace rb::engine::test;
using namespace std::chrono_literals;

namespace {

namespace wire = sb::wire;

constexpr u64 kEdgeTimeMs = 1'790'000'000'000;

[[nodiscard]] Uuid server_uuid(u8 seed) {
    Uuid id;
    id.bytes.fill(seed);
    id.bytes[6] = 0x40;
    return id;
}

[[nodiscard]] wire::ListEntry entry(u8 seed, std::string name) {
    wire::ListEntry out;
    out.handle = seed;
    out.id = server_uuid(seed);
    out.name = std::move(name);
    out.author = "author";
    out.version = "8.51";
    out.bucket = GameVersion::parse("8.51")->bucket();
    out.max_players = 100;
    out.flags = wire::entry_flag::online | wire::entry_flag::reachable;
    out.created_ms = kEdgeTimeMs;
    return out;
}

// The rbsb/1 edge's side of the browse connection, on its control stream.
class Edge {
public:
    explicit Edge(testing::FakeQuicPeer& peer) : peer_(peer) {}

    // The next T the engine sent, past frames of other types; time moves on until it comes.
    template <class T>
    [[nodiscard]] T next(EngineRig& rig) {
        REQUIRE(rig.strand.pump_until([&] {
            pull();
            while (!queue_.empty() && queue_.front().first != wire::frame_type_v<T>) queue_.pop_front();
            return !queue_.empty();
        }, 1min));
        T message;
        REQUIRE(wire::decode(queue_.front().second, message));
        queue_.pop_front();
        return message;
    }

    template <class T>
    void send(const T& message) {
        peer_.send(peer_.streams().front(), wire::frame_bytes(message), false);
    }

    void welcome() {
        wire::Welcome welcome;
        welcome.edge_id = 42;
        welcome.features = wire::feature::datagrams;
        welcome.server_time_ms = kEdgeTimeMs;
        welcome.limits.max_subscriptions = 8;
        welcome.limits.max_window = 200;
        welcome.limits.max_query_limit = 100;
        send(welcome);
    }

private:
    void pull() {
        if (peer_.streams().empty()) return;
        const std::vector<u8> all = peer_.received(peer_.streams().front());
        REQUIRE(wire::for_each_frame(std::span<const u8>(all).subspan(consumed_), 1 << 20, [&](const wire::FrameView& frame) {
            queue_.emplace_back(frame.type, std::vector<u8>(frame.payload.begin(), frame.payload.end()));
            return true;
        }));
        consumed_ = all.size();
    }

    testing::FakeQuicPeer& peer_;
    std::size_t consumed_ = 0;
    std::deque<std::pair<wire::FrameType, std::vector<u8>>> queue_;
};

// An open view keeps the browse connection up between searches, as a UI's server list does.
struct BrowserRig {
    BrowserRig() {
        rig.resolver->set(std::string(browser::kCompiledRbsbHost), {IpAddress::v4(0x0A000001)});
        rig.boot();
        client = rig.connect();
        REQUIRE(rig.call<api::BrowserOpenViewResponse>(*client, api::kBrowserOpenView, api::BrowserOpenViewRequest{}));
        testing::FakeQuicPeer* peer = nullptr;
        REQUIRE(rig.strand.pump_until([&] { return (peer = rig.quic.last()) != nullptr; }, 1min));
        peer->accept();
        edge.emplace(*peer);
        static_cast<void>(edge->next<wire::Hello>(rig));
        edge->welcome();
    }

    [[nodiscard]] u64 search(const api::BrowserSearchRequest& request) {
        const Result<u64> op = rig.start(*client, api::kBrowserSearch, request);
        REQUIRE(op);
        return *op;
    }

    EngineRig rig;
    std::unique_ptr<testing::ApiTestClient> client;
    std::optional<Edge> edge;
};

[[nodiscard]] api::BrowserSearchRequest text(std::string value) {
    api::BrowserSearchRequest request;
    request.text = std::move(value);
    return request;
}

}  // namespace

TEST_CASE("browser flow: a search asks the edge and pages with its cursor") {
    BrowserRig b;
    api::BrowserSearchRequest request = text(" arena ");
    request.sort = api::ServerSort::Name;
    request.limit = 500;
    const u64 first = b.search(request);
    const wire::Query query = b.edge->next<wire::Query>(b.rig);
    CHECK(query.text == "arena");
    CHECK(query.view.sort == wire::Sort::name);
    CHECK(query.limit == 100);
    CHECK(query.cursor.empty());
    b.edge->send(wire::QueryResult{query.req_id, {entry(1, "Arena one"), entry(2, "Arena two")}, {7, 7}, 40});
    const auto page = EngineRig::completed<api::BrowserSearchResponse>(b.rig.settle_op(*b.client, first, 1min));
    REQUIRE(page.rows.size() == 2);
    CHECK(page.rows[0].name == "Arena one");
    CHECK(page.next_cursor == api::Bytes{7, 7});
    CHECK(page.total == 40);
    CHECK_FALSE(page.resolved_by_id);

    request.cursor = page.next_cursor;
    const u64 second = b.search(request);
    const wire::Query next = b.edge->next<wire::Query>(b.rig);
    CHECK(next.cursor == wire::Bytes{7, 7});
    b.edge->send(wire::QueryResult{next.req_id, {entry(3, "Arena three")}, {}, 40});
    const auto last = EngineRig::completed<api::BrowserSearchResponse>(b.rig.settle_op(*b.client, second, 1min));
    REQUIRE(last.rows.size() == 1);
    CHECK(last.next_cursor.empty());
}

TEST_CASE("browser flow: a reboot:// link is resolved by id, hidden servers included") {
    BrowserRig b;
    const u64 op = b.search(text("reboot://" + format_uuid(server_uuid(5))));
    const wire::Resolve resolve = b.edge->next<wire::Resolve>(b.rig);
    CHECK(resolve.id == server_uuid(5));
    b.edge->send(wire::ResolveResult{resolve.req_id, wire::EntryDetails{entry(5, "Hidden"), "", 0}});
    const auto page = EngineRig::completed<api::BrowserSearchResponse>(b.rig.settle_op(*b.client, op, 1min));
    CHECK(page.resolved_by_id);
    REQUIRE(page.rows.size() == 1);
    CHECK(page.rows[0].name == "Hidden");
    CHECK(page.total == 1);
}

TEST_CASE("browser flow: a newer search supersedes the older one, and bad text starts none") {
    BrowserRig b;
    const u64 older = b.search(text("are"));
    const u64 newer = b.search(text("arena"));
    const api::Outcome superseded = b.rig.settle_op(*b.client, older, 1min);
    REQUIRE(superseded.cancelled);
    CHECK(*superseded.cancelled == api::CancelReason::Superseded);

    // The older one may have gone out before the newer one cancelled it.
    wire::Query query = b.edge->next<wire::Query>(b.rig);
    if (query.text != "arena") query = b.edge->next<wire::Query>(b.rig);
    CHECK(query.text == "arena");
    b.edge->send(wire::QueryResult{query.req_id, {}, {}, 0});
    CHECK(EngineRig::completed<api::BrowserSearchResponse>(b.rig.settle_op(*b.client, newer, 1min)).rows.empty());

    const Result<u64> blank = b.rig.start(*b.client, api::kBrowserSearch, text("   "));
    REQUIRE_FALSE(blank);
    CHECK(blank.error().id == "browser.search_text_length");
}
