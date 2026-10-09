#include <catch2/catch_test_macros.hpp>
#include <string>

#include "browser_test_support.hpp"
#include "reboot/browser/search.hpp"

using namespace rb;
using namespace rb::browser;
using namespace rb::browser::test;
using namespace std::chrono_literals;

namespace {

struct SearchRig : SessionRig {
    SearchRig() : search(session, own, rt.timers(), rt.clock()) {}

    [[nodiscard]] static SearchRequest text(std::string value) {
        SearchRequest request;
        request.text = std::move(value);
        return request;
    }

    FakeOwnServers own;
    Search search;
};

}  // namespace

TEST_CASE("search text must be 1 to 64 bytes once trimmed", "[browser][search]") {
    SearchRig rig;
    CHECK(rig.search.run(ConnectionId{1}, SearchRig::text("   "), {}, [](Result<SearchPage>) {}).error().id ==
          "browser.search_text_length");
    CHECK_FALSE(rig.search.run(ConnectionId{1}, SearchRig::text(std::string(65, 'a')), {}, [](Result<SearchPage>) {}));
    CHECK(rig.search.run(ConnectionId{1}, SearchRig::text(std::string(64, 'a')), {}, [](Result<SearchPage>) {}));
}

TEST_CASE("a client's searches are debounced and the older one is superseded", "[browser][search]") {
    SearchRig rig;
    BrowserLease lease = rig.session.acquire();
    Edge& edge = rig.connect();
    Capture<Result<SearchPage>> first;
    Capture<Result<SearchPage>> second;
    Capture<Result<SearchPage>> other;
    REQUIRE(rig.search.run(ConnectionId{1}, SearchRig::text("reb"), {}, first.callback()));
    rig.rt.advance(100ms);
    REQUIRE(rig.search.run(ConnectionId{1}, SearchRig::text(" reboot "), {}, second.callback()));
    REQUIRE(rig.search.run(ConnectionId{2}, SearchRig::text("other"), {}, other.callback()));
    REQUIRE(first.calls == 1);
    CHECK(first.result->error().kind == ErrorKind::Cancelled);
    CHECK(edge.idle());

    rig.rt.advance(kSearchDebounce);
    const wire::Query query = edge.expect<wire::Query>();
    CHECK(query.text == "reboot");
    CHECK(edge.expect<wire::Query>().text == "other");
    rig.own.ids.push_back(server_id(2));
    edge.send(wire::QueryResult{query.req_id, {entry(1, 1, "Reboot one"), entry(2, 2, "My reboot")}, {7, 7}, 40});
    rig.rt.run_until_idle();
    REQUIRE(second.calls == 1);
    const SearchPage& page = **second.result;
    REQUIRE(page.rows.size() == 1);
    CHECK(page.rows[0].name == "Reboot one");
    CHECK(page.next == SearchCursor{{7, 7}});
    CHECK(page.total == 40);
    CHECK_FALSE(page.resolved_by_id);
    CHECK(first.calls == 1);
}

TEST_CASE("search filters map onto the Query and the limit is clamped", "[browser][search]") {
    SearchRig rig;
    BrowserLease lease = rig.session.acquire();
    Edge& edge = rig.connect();
    SearchRequest request = SearchRig::text("name");
    request.bucket = GameVersion::parse("8.51")->bucket();
    request.password = PasswordFilter::Without;
    request.region = Region::Europe;
    request.sort = ServerSort::Name;
    request.cursor = SearchCursor{{1, 2, 3}};
    request.limit = 500;
    REQUIRE(rig.search.run(ConnectionId{1}, request, {}, [](Result<SearchPage>) {}));
    rig.rt.advance(kSearchDebounce);
    const wire::Query query = edge.expect<wire::Query>();
    CHECK(query.view.bucket == request.bucket);
    CHECK(query.view.password == wire::PasswordFilter::none);
    CHECK(query.view.region == wire::Region::europe);
    CHECK(query.view.sort == wire::Sort::name);
    CHECK(query.cursor == wire::Bytes{1, 2, 3});
    CHECK(query.limit == 100);
}

TEST_CASE("a link or bare id is resolved by id instead", "[browser][search]") {
    SearchRig rig;
    BrowserLease lease = rig.session.acquire();
    Edge& edge = rig.connect();
    Capture<Result<SearchPage>> found;
    REQUIRE(rig.search.run(ConnectionId{1}, SearchRig::text("reboot://" + format_uuid(server_id(3).value)), {},
                           found.callback()));
    rig.rt.advance(kSearchDebounce);
    const wire::Resolve resolve = edge.expect<wire::Resolve>();
    CHECK(resolve.id == server_id(3).value);
    edge.send(wire::ResolveResult{resolve.req_id, wire::EntryDetails{entry(1, 3, "Offline", "8.51", 0, 0), "", 0}});
    rig.rt.run_until_idle();
    REQUIRE(found.calls == 1);
    CHECK((*found.result)->resolved_by_id);
    REQUIRE((*found.result)->rows.size() == 1);
    CHECK_FALSE((*found.result)->rows[0].online);

    Capture<Result<SearchPage>> missing;
    REQUIRE(rig.search.run(ConnectionId{1}, SearchRig::text(format_uuid(server_id(4).value)), {}, missing.callback()));
    rig.rt.advance(kSearchDebounce);
    edge.send(wire::Error{edge.expect<wire::Resolve>().req_id, wire::ErrorCode::not_found, {}, 0});
    rig.rt.run_until_idle();
    REQUIRE(missing.calls == 1);
    CHECK((*missing.result)->rows.empty());
    CHECK((*missing.result)->total == 0);
}

TEST_CASE("RATE_LIMITED is retried after the edge's hint", "[browser][search]") {
    SearchRig rig;
    BrowserLease lease = rig.session.acquire();
    Edge& edge = rig.connect();
    Capture<Result<SearchPage>> done;
    REQUIRE(rig.search.run(ConnectionId{1}, SearchRig::text("busy"), {}, done.callback()));
    rig.rt.advance(kSearchDebounce);
    edge.send(wire::Error{edge.expect<wire::Query>().req_id, wire::ErrorCode::rate_limited, {}, 800});
    rig.rt.run_until_idle();
    CHECK(done.calls == 0);
    rig.rt.advance(799ms);
    CHECK(edge.idle());
    rig.rt.advance(1ms);
    const wire::Query retry = edge.expect<wire::Query>();
    edge.send(wire::QueryResult{retry.req_id, {}, {}, 0});
    rig.rt.run_until_idle();
    REQUIRE(done.calls == 1);
    CHECK(*done.result);
}

TEST_CASE("cancelling a search ends it once", "[browser][search][race]") {
    SearchRig rig;
    BrowserLease lease = rig.session.acquire();
    Edge& edge = rig.connect();
    CancelSource cancel;
    Capture<Result<SearchPage>> done;
    REQUIRE(rig.search.run(ConnectionId{1}, SearchRig::text("abc"), cancel.token(), done.callback()));
    rig.rt.advance(kSearchDebounce);
    const wire::Query query = edge.expect<wire::Query>();
    cancel.cancel(CancelReason::User);
    edge.send(wire::QueryResult{query.req_id, {}, {}, 0});
    rig.rt.run_until_idle();
    REQUIRE(done.calls == 1);
    CHECK(done.result->error().kind == ErrorKind::Cancelled);
}
