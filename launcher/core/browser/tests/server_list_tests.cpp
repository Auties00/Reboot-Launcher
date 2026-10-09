#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <string>
#include <vector>

#include "browser_test_support.hpp"
#include "reboot/browser/server_list.hpp"
#include "reboot/foundation/events.hpp"

using namespace reboot;
using namespace reboot::browser;
using namespace reboot::browser::test;
using namespace std::chrono_literals;

namespace {

struct ListRig : SessionRig {
    ListRig()
        : list(session, own, rt.clock(), rt.events(), BrowseChoices{},
               [this](const BrowseChoices& choices) { persisted.push_back(choices); }) {}

    // Opens `spec`, connects and answers each Subscribe with a SubOpen; returns them in order.
    std::vector<wire::Subscribe> open(const ViewSpec& spec, ViewId& id) {
        auto opened = list.open(spec);
        REQUIRE(opened);
        id = *opened;
        Edge& edge = connect();
        std::vector<wire::Subscribe> subscribes;
        while (!edge.idle()) {
            const wire::Subscribe subscribe = edge.expect<wire::Subscribe>();
            edge.send(wire::SubOpen{subscribe.req_id, subscribe.sub_id, 100 + subscribe.sub_id, subscribe.window});
            subscribes.push_back(subscribe);
        }
        rt.run_until_idle();
        return subscribes;
    }

    [[nodiscard]] std::vector<std::string> names(ViewId id) const {
        std::vector<std::string> out;
        for (const ServerRow& row : list.current(id)->rows) out.push_back(row.name);
        return out;
    }

    FakeOwnServers own;
    std::vector<BrowseChoices> persisted;
    ServerList list;
};

[[nodiscard]] ViewSpec spec_for(std::vector<u32> buckets = {}) {
    ViewSpec spec;
    spec.versions.buckets = std::move(buckets);
    spec.sort = ServerSort::Players;
    return spec;
}

}  // namespace

TEST_CASE("a view loads from its snapshot, sorted and without own servers", "[browser][list]") {
    ListRig rig;
    rig.own.ids.push_back(server_id(3));
    testing::EventRecorder recorder(rig.rt.events(), EventFilter{{EventKind::ViewSnapshot, EventKind::ViewDelta}, {}, {}});
    ViewId id;
    const auto subscribes = rig.open(spec_for(), id);
    REQUIRE(subscribes.size() == 1);
    CHECK(subscribes[0].view.bucket == wire::kBucketAll);
    CHECK(subscribes[0].window == kSmallWindow);
    CHECK(rig.list.current(id)->state == ListState::Loading);

    Edge& edge = *rig.edges.back();
    edge.snapshot(wire::Snapshot{101, 1, 3, {entry(1, 1, "Few", "8.51", 1), entry(2, 2, "Many", "8.51", 9),
                                            entry(3, 3, "Mine", "8.51", 50)}});
    rig.rt.run_until_idle();
    const ViewUpdate* update = rig.list.current(id);
    CHECK(update->state == ListState::Ready);
    CHECK(rig.names(id) == std::vector<std::string>{"Many", "Few"});
    CHECK(update->total == 2);
    CHECK_FALSE(update->total_approximate);
    // Edge time minus the Welcome offset: two seconds before the local clock's zero.
    CHECK(update->rows[0].created_at == std::chrono::system_clock::time_point(-2s));

    wire::Patch patch;
    patch.handle = 1;
    patch.vseq = 2;
    patch.players = 20;
    edge.datagram(wire::Delta{101, {patch}, std::nullopt});
    rig.rt.run_until_idle();
    CHECK(rig.names(id) == std::vector<std::string>{"Few", "Many"});

    recorder.pump();
    CHECK(recorder.count(EventKind::ViewSnapshot) == 2);
    CHECK(recorder.count(EventKind::ViewDelta) == 1);
}

TEST_CASE("installed buckets are merged, deduplicated and filtered to the exact build", "[browser][list]") {
    ListRig rig;
    const GameVersion wanted = *GameVersion::parse("8.51");
    ViewSpec spec = spec_for({wanted.bucket(), GameVersion::parse("9.10")->bucket()});
    spec.versions.exact_version = wanted;
    ViewId id;
    const auto subscribes = rig.open(spec, id);
    REQUIRE(subscribes.size() == 2);
    Edge& edge = *rig.edges.back();
    edge.snapshot(wire::Snapshot{100 + subscribes[0].sub_id, 1, 2,
                                 {entry(1, 1, "Exact", "8.51", 5), entry(2, 2, "Other patch", "8.51.1", 4)}},
                  3);
    rig.rt.run_until_idle();
    CHECK(rig.list.current(id)->state == ListState::Loading);
    edge.snapshot(wire::Snapshot{100 + subscribes[1].sub_id, 1, 1, {entry(7, 3, "Nine", "9.10", 9)}}, 7);
    rig.rt.run_until_idle();
    CHECK(rig.list.current(id)->state == ListState::Ready);
    CHECK(rig.names(id) == std::vector<std::string>{"Exact"});
}

TEST_CASE("a server seen in two buckets' views is listed once", "[browser][list]") {
    ListRig rig;
    ViewId id;
    const auto subscribes =
        rig.open(spec_for({GameVersion::parse("8.51")->bucket(), GameVersion::parse("9.10")->bucket()}), id);
    REQUIRE(subscribes.size() == 2);
    Edge& edge = *rig.edges.back();
    // The server moved to 9.10; the 8.51 view has not dropped it yet.
    edge.snapshot(wire::Snapshot{100 + subscribes[0].sub_id, 1, 2,
                                 {entry(1, 1, "Moved", "8.51", 3), entry(2, 2, "Stays", "8.51", 1)}},
                  3);
    edge.snapshot(wire::Snapshot{100 + subscribes[1].sub_id, 1, 1, {entry(5, 1, "Moved", "9.10", 3)}}, 7);
    rig.rt.run_until_idle();
    REQUIRE(rig.list.current(id)->state == ListState::Ready);
    CHECK(rig.names(id) == std::vector<std::string>{"Moved", "Stays"});
    CHECK(rig.list.current(id)->total == 2);
}

TEST_CASE("a dropped connection leaves the rows Stale until a new snapshot replaces them", "[browser][list]") {
    ListRig rig;
    ViewId id;
    rig.open(spec_for(), id);
    Edge& first = *rig.edges.back();
    first.snapshot(wire::Snapshot{101, 1, 1, {entry(1, 1, "Old")}});
    rig.rt.run_until_idle();
    REQUIRE(rig.list.current(id)->state == ListState::Ready);

    rig.rt.clock().set_system(std::chrono::system_clock::time_point(1h));
    first.peer().close(std::nullopt);
    rig.rt.run_until_idle();
    const ViewUpdate* stale = rig.list.current(id);
    CHECK(stale->state == ListState::Stale);
    CHECK(stale->stale_since == std::chrono::system_clock::time_point(1h));
    CHECK(rig.names(id) == std::vector<std::string>{"Old"});

    rig.rt.advance(FullJitterBackoff::kBase);
    Edge& second = rig.connect();
    const wire::Subscribe replay = second.expect<wire::Subscribe>();
    second.send(wire::SubOpen{replay.req_id, replay.sub_id, 7, 50});
    second.snapshot(wire::Snapshot{7, 1, 1, {entry(4, 4, "New")}});
    rig.rt.run_until_idle();
    CHECK(rig.list.current(id)->state == ListState::Ready);
    CHECK(rig.names(id) == std::vector<std::string>{"New"});
}

TEST_CASE("more buckets than the edge allows fall back to every version filtered here", "[browser][list]") {
    ListRig rig;
    BrowserLease lease = rig.session.acquire();
    Edge& edge = rig.connect(2);
    const std::vector<u32> buckets{GameVersion::parse("7.40")->bucket(), GameVersion::parse("8.51")->bucket(),
                                   GameVersion::parse("9.10")->bucket()};
    auto opened = rig.list.open(spec_for(buckets));
    REQUIRE(opened);
    const wire::Subscribe first = edge.expect<wire::Subscribe>();
    const wire::Subscribe second = edge.expect<wire::Subscribe>();
    CHECK(first.view.bucket == buckets[0]);
    CHECK(second.view.bucket == buckets[1]);
    const u32 dropped_a = edge.expect<wire::Unsubscribe>().sub_id;
    const u32 dropped_b = edge.expect<wire::Unsubscribe>().sub_id;
    CHECK(std::min(dropped_a, dropped_b) == first.sub_id);
    CHECK(std::max(dropped_a, dropped_b) == second.sub_id);
    const wire::Subscribe fallback = edge.expect<wire::Subscribe>();
    CHECK(fallback.view.bucket == wire::kBucketAll);

    edge.send(wire::SubOpen{fallback.req_id, fallback.sub_id, 1, 50});
    edge.snapshot(wire::Snapshot{1, 1, 60, {entry(1, 1, "Seven", "7.40"), entry(2, 2, "Twelve", "12.41")}});
    rig.rt.run_until_idle();
    const ViewUpdate* update = rig.list.current(*opened);
    CHECK(rig.names(*opened) == std::vector<std::string>{"Seven"});
    CHECK(update->total == 60);
    CHECK(update->total_approximate);
}

TEST_CASE("update re-subscribes only what changed", "[browser][list]") {
    ListRig rig;
    ViewId id;
    rig.open(spec_for(), id);
    Edge& edge = *rig.edges.back();
    edge.snapshot(wire::Snapshot{101, 1, 2, {entry(1, 1, "A", "8.51"), entry(2, 2, "B", "9.10")}});
    rig.rt.run_until_idle();

    ViewSpec exact = spec_for();
    exact.versions.exact_version = *GameVersion::parse("9.10");
    REQUIRE(rig.list.update(id, exact));
    CHECK(edge.idle());
    CHECK(rig.list.current(id)->state == ListState::Ready);
    CHECK(rig.names(id) == std::vector<std::string>{"B"});

    ViewSpec sorted = exact;
    sorted.sort = ServerSort::Name;
    REQUIRE(rig.list.update(id, sorted));
    rig.rt.run_until_idle();
    CHECK(rig.list.current(id)->state == ListState::Loading);
    CHECK(edge.expect<wire::Unsubscribe>().sub_id != 0);
    CHECK(edge.expect<wire::Subscribe>().view.sort == wire::Sort::name);

    ViewSpec invalid;
    invalid.window = 0;
    CHECK(rig.list.update(id, invalid).error().id == "browser.invalid_view_spec");
    rig.list.close(id);
    CHECK(rig.list.current(id) == nullptr);
    CHECK(rig.list.open_views().empty());
}

TEST_CASE("browse choices are kept and persisted", "[browser][list]") {
    ListRig rig;
    BrowseChoices choices;
    choices.versions = VersionScope::Installed;
    choices.sort = ServerSort::Newest;
    rig.list.set_choices(choices);
    CHECK(rig.list.choices() == choices);
    REQUIRE(rig.persisted.size() == 1);
    CHECK(rig.persisted[0] == choices);
}
