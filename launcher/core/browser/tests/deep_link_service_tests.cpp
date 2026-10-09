#include <any>
#include <catch2/catch_test_macros.hpp>
#include <string>

#include "browser_test_support.hpp"
#include "reboot/browser/deep_link_service.hpp"
#include "reboot/browser/game_server_target.hpp"
#include "reboot/browser/join_prompts.hpp"
#include "reboot/foundation/user_request.hpp"
#include "reboot/net/datagram_connector.hpp"
#include "reboot/net/udp_beacon_prober.hpp"

using namespace reboot;
using namespace reboot::browser;
using namespace reboot::browser::test;

namespace {

class NoConnector final : public net::IDatagramConnector {
public:
    Result<std::unique_ptr<net::IDatagramChannel>> connect(Endpoint, net::DatagramCallbacks) override {
        return make_diag(ErrorDomain::Net, MessageId{"net.udp_socket_failed"}).fail();
    }
};

struct LinkRig : SessionRig {
    LinkRig()
        : target(resolver, prober, rt.ops(), rt.events(), std::nullopt, nullptr),
          links(session, target, own, rt.requests(), rt.ops()) {}

    [[nodiscard]] std::string link(u8 seed) const { return "Reboot://" + format_uuid(server_id(seed).value) + "/"; }

    // Answers the next Resolve with `listed`, or with no details.
    void answer(Edge& edge, std::optional<wire::ListEntry> listed) {
        const wire::Resolve resolve = edge.expect<wire::Resolve>();
        wire::ResolveResult result{resolve.req_id, std::nullopt};
        if (listed) result.details = wire::EntryDetails{*listed, "Hidden server", kEdgeTimeMs};
        edge.send(result);
        rt.run_until_idle();
    }

    [[nodiscard]] std::optional<ErasedOutcome> outcome(const Result<OpHandle>& handle) {
        REQUIRE(handle);
        return rt.ops().outcome(handle->id());
    }

    NoConnector connector;
    net::UdpBeaconProber prober{connector, rt.strand(), rt.timers(), rt.clock()};
    FakeOwnServers own;
    GameServerTarget target;
    DeepLinkService links;
};

}  // namespace

TEST_CASE("bad links and own servers fail before anything is sent", "[browser][link]") {
    LinkRig rig;
    CHECK(rig.links.start_resolve("https://example.com", DisconnectPolicy::Detached).error().id == "browser.invalid_link");
    rig.own.ids.push_back(server_id(1));
    CHECK(rig.links.start_resolve(rig.link(1), DisconnectPolicy::Detached).error().id == "browser.join_own_server");
    rig.rt.run_until_idle();
    CHECK(rig.quic.connections().empty());
    CHECK(rig.rt.ops().live().empty());
}

TEST_CASE("an accepted link becomes the join target, and only then", "[browser][link]") {
    LinkRig rig;
    const Result<OpHandle> handle = rig.links.start_resolve(rig.link(2), DisconnectPolicy::Detached);
    REQUIRE(handle);
    Edge& edge = rig.connect();
    rig.answer(edge, entry(1, 2, "Hidden", "8.51", 0, wire::entry_flag::hidden));

    std::vector<UserRequest> pending = rig.rt.requests().pending();
    REQUIRE(pending.size() == 1);
    CHECK(pending[0].kind == UserRequestKind::ConfirmJoin);
    CHECK(std::any_cast<ConfirmJoinPrompt>(pending[0].payload).server.row.hidden);
    CHECK_FALSE(rig.target.current());
    REQUIRE(rig.rt.requests().respond(pending[0].id, ConfirmJoinAnswer{true}));

    REQUIRE(rig.target.current());
    CHECK(std::get<ServerTarget>(rig.target.current()->target) == ServerTarget{server_id(2), "Hidden", "author"});
    const auto result = rig.outcome(handle);
    REQUIRE(result);
    const auto* completed = std::get_if<Completed<std::any>>(&*result);
    REQUIRE(completed);
    CHECK(std::any_cast<LinkResolution>(completed->value).server.description == "Hidden server");
    // Resolving never joins.
    CHECK(edge.idle());
}

TEST_CASE("a declined or unknown link leaves the target alone", "[browser][link]") {
    LinkRig rig;
    BrowserLease lease = rig.session.acquire();
    Edge& edge = rig.connect();

    const Result<OpHandle> gone = rig.links.start_resolve(rig.link(3), DisconnectPolicy::Detached);
    rig.rt.run_until_idle();
    rig.answer(edge, std::nullopt);
    CHECK(std::get<Failed>(*rig.outcome(gone)).error.id == "browser.link_not_found");

    const Result<OpHandle> declined = rig.links.start_resolve(rig.link(4), DisconnectPolicy::Detached);
    rig.rt.run_until_idle();
    rig.answer(edge, entry(1, 4, "Four"));
    REQUIRE(rig.rt.requests().respond(rig.rt.requests().pending().front().id, ConfirmJoinAnswer{false}));
    CHECK(std::get<Failed>(*rig.outcome(declined)).error.id == "browser.join_refused");
    CHECK_FALSE(rig.target.current());
}

TEST_CASE("a newer link supersedes a pending one", "[browser][link][race]") {
    LinkRig rig;
    BrowserLease lease = rig.session.acquire();
    Edge& edge = rig.connect();
    const Result<OpHandle> first = rig.links.start_resolve(rig.link(5), DisconnectPolicy::Detached);
    rig.rt.run_until_idle();
    rig.answer(edge, entry(1, 5, "Five"));
    REQUIRE(rig.rt.requests().pending().size() == 1);

    const Result<OpHandle> second = rig.links.start_resolve(rig.link(6), DisconnectPolicy::Detached);
    REQUIRE(second);
    const auto superseded = rig.outcome(first);
    REQUIRE(superseded);
    CHECK(std::get<Cancelled>(*superseded).reason == CancelReason::Superseded);
    CHECK(rig.rt.requests().pending().empty());

    rig.rt.run_until_idle();
    rig.answer(edge, entry(2, 6, "Six"));
    REQUIRE(rig.rt.requests().respond(rig.rt.requests().pending().front().id, ConfirmJoinAnswer{true}));
    CHECK(std::get<ServerTarget>(rig.target.current()->target).name == "Six");
}
