#include <algorithm>
#include <chrono>
#include <deque>
#include <map>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "net_test_support.hpp"
#include "reboot/foundation/events.hpp"
#include "reboot/foundation/executor.hpp"
#include "reboot/net/port_mapper_service.hpp"
#include "reboot/net/port_mapping_gateway.hpp"
#include "reboot/testing/event_recorder.hpp"

using namespace rb;
using namespace rb::net;
using namespace std::chrono_literals;
using rb::net::test::TestStrand;

namespace {

constexpr std::string_view kTag = "00112233aabbccdd";
const IpAddress kLan = IpAddress::v4(0xC0A80105);

// A router in memory. Thread-safe: the service calls it from workers.
class FakeGateway final : public IPortMappingGateway {
public:
    explicit FakeGateway(MappingMethod method) : method_(method) {}

    [[nodiscard]] MappingMethod method() const noexcept override { return method_; }

    std::expected<GatewayInfo, GatewayError> discover(std::chrono::milliseconds, const CancelToken&) override {
        const std::scoped_lock lock(mutex_);
        ++discovers;
        if (!present) return std::unexpected(GatewayError{GatewayErrorCode::NoGateway});
        return GatewayInfo{kLan, IpAddress::v4(0x01020304)};
    }

    std::expected<PortMapping, GatewayError> add(const GatewayMappingRequest& request, std::chrono::milliseconds) override {
        const std::scoped_lock lock(mutex_);
        adds.push_back(request);
        if (auto it = add_errors.find(request.external.value); it != add_errors.end() && !it->second.empty()) {
            const GatewayError error = it->second.front();
            it->second.pop_front();
            return std::unexpected(error);
        }
        if (permanent_only && request.lease != 0s) return std::unexpected(GatewayError{GatewayErrorCode::OnlyPermanentLease, 725});
        Port external = request.external;
        if (auto moved = move_external.find(request.internal.value); moved != move_external.end()) external = moved->second;
        const PortMapping mapping{request.internal, external, method_, granted_lease.value_or(request.lease), request.lan_address};
        entries[external.value] = GatewayEntry{request.internal, external, request.lan_address, request.description};
        return mapping;
    }

    std::expected<void, GatewayError> remove(const PortMapping& mapping, std::chrono::milliseconds) override {
        const std::scoped_lock lock(mutex_);
        removes.push_back(mapping);
        entries.erase(mapping.external.value);
        return {};
    }

    std::expected<std::vector<GatewayEntry>, GatewayError> list(std::chrono::milliseconds) override {
        const std::scoped_lock lock(mutex_);
        if (method_ == MappingMethod::NatPmp) return std::unexpected(GatewayError{GatewayErrorCode::Unsupported});
        std::vector<GatewayEntry> out;
        for (const auto& [port, entry] : entries) out.push_back(entry);
        return out;
    }

    template <class F>
    auto locked(F&& read) {
        const std::scoped_lock lock(mutex_);
        return read(*this);
    }

    bool present = true;
    bool permanent_only = false;
    std::optional<std::chrono::seconds> granted_lease;
    std::map<u16, std::deque<GatewayError>> add_errors;
    std::map<u16, Port> move_external;
    int discovers = 0;
    std::vector<GatewayMappingRequest> adds;
    std::vector<PortMapping> removes;
    std::map<u16, GatewayEntry> entries;

private:
    MappingMethod method_;
    std::mutex mutex_;
};

SessionId session(char digit) {
    std::string text = "00000000-0000-4000-8000-000000000000";
    text[0] = digit;
    const Result<Uuid> uuid = parse_uuid(text);
    REQUIRE(uuid);
    return SessionId{*uuid};
}

struct Fixture {
    explicit Fixture(std::vector<MappingRecord> recorded = {})
        : service(upnp, natpmp, std::string(kTag), std::move(recorded),
                  [this](std::vector<MappingRecord> records) { persisted.push_back(std::move(records)); }, workers, strand,
                  timers, events) {}

    ManualClock clock;
    TestStrand strand{clock};
    TimerService timers{clock, strand};
    EventBus events{EngineEpoch{1}};
    testing::EventRecorder recorder{events};
    WorkerPool workers{2};
    FakeGateway upnp{MappingMethod::Upnp};
    FakeGateway natpmp{MappingMethod::NatPmp};
    std::vector<std::vector<MappingRecord>> persisted;
    PortMapperService service;

    // Waits for the workers until a PortMappingChanged arrives.
    std::vector<PortMappingChanged> wait_for_change() {
        std::vector<PortMappingChanged> out;
        strand.run_until([&] {
            recorder.pump();
            for (const PortMappingChanged* change : recorder.payloads<PortMappingChanged>(EventKind::PortMappingChanged))
                out.push_back(*change);
            recorder.clear();
            return !out.empty();
        });
        return out;
    }
};

const std::vector<Port> kBlock{Port{7777}, Port{7778}};

}  // namespace

TEST_CASE("a block maps over UPnP at its own ports, with a description and a sweep record", "[net][mapping]") {
    Fixture f;
    REQUIRE(f.service.map(session('a'), kBlock));
    const std::vector<PortMappingChanged> changes = f.wait_for_change();
    REQUIRE(changes.size() == 1);
    const PortMappingChanged& change = changes.front();
    CHECK(change.session == session('a'));
    CHECK(change.game_port == Port{7777});
    CHECK_FALSE(change.failure);
    REQUIRE(change.mappings.size() == 2);
    CHECK(change.mappings[0].method == MappingMethod::Upnp);
    CHECK(change.mappings[0].lease == kMappingLease);
    CHECK(change.mappings[0].lan_address == kLan);
    CHECK(change.granted_game_port() == Port{7777});
    CHECK(f.service.mappings(session('a')).size() == 2);

    const auto adds = f.upnp.locked([](FakeGateway& g) { return g.adds; });
    REQUIRE(adds.size() == 2);
    CHECK(adds[0].description == mapping_description(kTag, session('a')));
    REQUIRE_FALSE(f.persisted.empty());
    CHECK(f.persisted.back().size() == 2);
    CHECK(f.persisted.back().front().session == session('a'));
    CHECK(f.natpmp.locked([](FakeGateway& g) { return g.discovers; }) == 0);
}

TEST_CASE("error 725 maps with lease 0 and 718 moves to another external port", "[net][mapping]") {
    Fixture f;
    f.upnp.permanent_only = true;
    f.upnp.add_errors[7777].push_back(GatewayError{GatewayErrorCode::ExternalPortTaken, 718});
    REQUIRE(f.service.map(session('b'), kBlock));
    const PortMappingChanged change = f.wait_for_change().front();
    REQUIRE(change.mappings.size() == 2);
    CHECK(change.mappings[0].lease == 0s);
    CHECK(change.granted_game_port() == Port{7778});
    // 7778 was granted to the game port, so the second port skips it.
    CHECK(change.mappings[1].external == Port{7779});
    // A permanent entry is never renewed.
    f.strand.advance(kMappingLease);
    CHECK(f.upnp.locked([](FakeGateway& g) { return g.adds.size(); }) == 5);
}

TEST_CASE("NAT-PMP is used when no UPnP gateway answers", "[net][mapping]") {
    Fixture f;
    f.upnp.present = false;
    REQUIRE(f.service.map(session('c'), kBlock));
    const PortMappingChanged change = f.wait_for_change().front();
    REQUIRE(change.mappings.size() == 2);
    CHECK(change.mappings[0].method == MappingMethod::NatPmp);
}

TEST_CASE("without any gateway, discovery is tried three times and only a diagnostic results", "[net][mapping]") {
    Fixture f;
    f.upnp.present = false;
    f.natpmp.present = false;
    REQUIRE(f.service.map(session('d'), kBlock));
    for (int round = 1; round < 3; ++round) {
        // The failed round arms the retry timer when its job comes back.
        f.strand.run_until([&] { return f.strand.timed_pending() > 0; });
        CHECK(f.natpmp.locked([](FakeGateway& g) { return g.discovers; }) == round);
        f.strand.advance(kGatewayDiscoveryRetryDelay);
    }
    const std::vector<PortMappingChanged> changes = f.wait_for_change();
    REQUIRE(changes.size() == 1);
    REQUIRE(changes.front().failure);
    CHECK(changes.front().failure->id == "net.no_gateway");
    CHECK(changes.front().mappings.empty());
    CHECK(f.upnp.locked([](FakeGateway& g) { return g.discovers; }) == 3);
}

TEST_CASE("renewal runs at half the lease and reports only moved or lost ports", "[net][mapping]") {
    Fixture f;
    REQUIRE(f.service.map(session('e'), kBlock));
    f.wait_for_change();
    f.strand.advance(kMappingLease / 2 - 1s);
    CHECK(f.upnp.locked([](FakeGateway& g) { return g.adds.size(); }) == 2);
    f.strand.advance(1s);
    f.strand.run_until([&] { return f.upnp.locked([](FakeGateway& g) { return g.adds.size(); }) == 4 && f.strand.timed_pending() > 0; });

    // An unchanged renewal publishes nothing; the next one moves the game port.
    f.upnp.locked([](FakeGateway& g) {
        g.move_external[7777] = Port{9000};
        return 0;
    });
    f.strand.advance(kMappingLease / 2);
    const PortMappingChanged moved = f.wait_for_change().front();
    CHECK(moved.granted_game_port() == Port{9000});
    CHECK_FALSE(moved.failure);

    f.upnp.locked([](FakeGateway& g) {
        g.add_errors[7778].push_back(GatewayError{GatewayErrorCode::Refused, 606});
        return 0;
    });
    f.strand.advance(kMappingLease / 2);
    const PortMappingChanged lost = f.wait_for_change().front();
    REQUIRE(lost.failure);
    CHECK(lost.failure->id == "net.mapping_renew_failed");
    REQUIRE(lost.mappings.size() == 1);
    CHECK(lost.mappings.front().internal == Port{7777});
    CHECK(f.persisted.back().size() == 1);
}

TEST_CASE("unmap deletes the entries, publishes and forgets the records", "[net][mapping]") {
    Fixture f;
    REQUIRE(f.service.map(session('f'), kBlock));
    f.wait_for_change();
    bool done = false;
    f.service.unmap(session('f'), [&] { done = true; });
    const PortMappingChanged change = f.wait_for_change().front();
    CHECK(change.mappings.empty());
    CHECK_FALSE(change.failure);
    f.strand.run_until([&] { return done; });
    CHECK(f.upnp.locked([](FakeGateway& g) { return g.removes.size(); }) == 2);
    CHECK(f.persisted.back().empty());
    CHECK(f.service.mappings(session('f')).empty());
    // The session can be mapped again.
    CHECK(f.service.map(session('f'), kBlock));
    f.wait_for_change();
}

TEST_CASE("unmap during mapping waits for the job, then removes what it granted", "[net][mapping][race]") {
    Fixture f;
    REQUIRE(f.service.map(session('7'), kBlock));
    bool done = false;
    f.service.unmap(session('7'), [&] { done = true; });
    bool again = false;
    f.service.unmap(session('7'), [&] { again = true; });
    f.strand.run_until([&] { return done && again; });
    const auto [added, removed] = f.upnp.locked([](FakeGateway& g) { return std::pair{g.adds.size(), g.removes.size()}; });
    CHECK(removed == added);
    CHECK(f.service.mappings(session('7')).empty());
    CHECK((f.persisted.empty() || f.persisted.back().empty()));
}

TEST_CASE("map refuses an empty block or a mapped session; unmapping nothing still answers", "[net][mapping]") {
    Fixture f;
    CHECK(f.service.map(session('8'), {}).error().id == "net.mapping_block_invalid");
    REQUIRE(f.service.map(session('8'), kBlock));
    CHECK(f.service.map(session('8'), kBlock).error().id == "net.mapping_block_invalid");
    bool done = false;
    f.service.unmap(session('9'), [&] { done = true; });
    f.strand.run_until([&] { return done; });
    f.wait_for_change();
}

TEST_CASE("unmap_all unmaps every session before answering", "[net][mapping]") {
    Fixture f;
    REQUIRE(f.service.map(session('1'), kBlock));
    REQUIRE(f.service.map(session('2'), std::vector<Port>{Port{7790}}));
    f.strand.run_until([&] { return f.service.mappings(session('1')).size() == 2 && f.service.mappings(session('2')).size() == 1; });
    bool done = false;
    f.service.unmap_all([&] { done = true; });
    f.strand.run_until([&] { return done; });
    CHECK(f.service.mappings(session('1')).empty());
    CHECK(f.service.mappings(session('2')).empty());
    CHECK(f.upnp.locked([](FakeGateway& g) { return g.entries.empty(); }));

    bool none = false;
    f.service.unmap_all([&] { none = true; });
    f.strand.run_until([&] { return none; });
}

TEST_CASE("the startup sweep deletes stale records and our stale UPnP entries only", "[net][mapping]") {
    const PortMapping stale_upnp{Port{7777}, Port{7777}, MappingMethod::Upnp, kMappingLease, kLan};
    const PortMapping stale_natpmp{Port{7778}, Port{7778}, MappingMethod::NatPmp, kMappingLease, kLan};
    const PortMapping live{Port{7790}, Port{7790}, MappingMethod::Upnp, kMappingLease, kLan};
    Fixture f({{session('1'), stale_upnp}, {session('1'), stale_natpmp}, {session('2'), live}});
    f.upnp.locked([&](FakeGateway& g) {
        g.entries[7790] = GatewayEntry{Port{7790}, Port{7790}, kLan, mapping_description(kTag, session('2'))};
        g.entries[7800] = GatewayEntry{Port{7800}, Port{7800}, kLan, mapping_description(kTag, session('3'))};
        g.entries[7801] = GatewayEntry{Port{7801}, Port{7801}, kLan, mapping_description("ffffffffffffffff", session('3'))};
        g.entries[7802] = GatewayEntry{Port{7802}, Port{7802}, IpAddress::v4(0xC0A80109), mapping_description(kTag, session('3'))};
        g.entries[7803] = GatewayEntry{Port{7803}, Port{7803}, kLan, "Somebody Else"};
        return 0;
    });
    bool done = false;
    const std::vector<SessionId> live_sessions{session('2')};
    f.service.sweep_stale(live_sessions, [&] { done = true; });
    f.strand.run_until([&] { return done; });

    const auto upnp_removes = f.upnp.locked([](FakeGateway& g) { return g.removes; });
    std::vector<u16> removed;
    for (const PortMapping& mapping : upnp_removes) removed.push_back(mapping.external.value);
    std::ranges::sort(removed);
    CHECK(removed == std::vector<u16>{7777, 7800});
    const auto natpmp_removes = f.natpmp.locked([](FakeGateway& g) { return g.removes; });
    REQUIRE(natpmp_removes.size() == 1);
    CHECK(natpmp_removes.front().internal == Port{7778});
    REQUIRE_FALSE(f.persisted.empty());
    REQUIRE(f.persisted.back().size() == 1);
    CHECK(f.persisted.back().front().session == session('2'));
}
