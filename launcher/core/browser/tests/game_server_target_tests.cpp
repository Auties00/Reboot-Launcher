#include <catch2/catch_test_macros.hpp>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <vector>

#include "browser_test_support.hpp"
#include "reboot/browser/game_server_target.hpp"
#include "reboot/foundation/events.hpp"
#include "reboot/net/datagram_connector.hpp"
#include "reboot/net/udp_beacon_prober.hpp"

using namespace rb;
using namespace rb::browser;
using namespace rb::browser::test;
using namespace std::chrono_literals;

namespace {

// Game ports in `alive` answer every probe; the others stay silent.
class EchoConnector final : public net::IDatagramConnector {
public:
    explicit EchoConnector(Executor& strand) : strand_(strand) {}

    Result<std::unique_ptr<net::IDatagramChannel>> connect(Endpoint target, net::DatagramCallbacks callbacks) override {
        probed.push_back(target);
        return std::make_unique<Channel>(*this, target, std::move(callbacks));
    }

    std::set<Endpoint> alive;
    std::vector<Endpoint> probed;

private:
    class Channel final : public net::IDatagramChannel {
    public:
        Channel(EchoConnector& owner, Endpoint target, net::DatagramCallbacks callbacks)
            : owner_(owner), target_(target), state_(std::make_shared<State>(std::move(callbacks))) {}
        ~Channel() override { state_->open = false; }

        void send(std::span<const u8> bytes) override {
            if (!owner_.alive.contains(target_)) return;
            owner_.strand_.post([state = state_, reply = std::vector<u8>(bytes.begin(), bytes.end())] {
                if (state->open && state->callbacks.on_datagram) state->callbacks.on_datagram(reply);
            });
        }

    private:
        struct State {
            explicit State(net::DatagramCallbacks callbacks_in) : callbacks(std::move(callbacks_in)) {}
            net::DatagramCallbacks callbacks;
            bool open = true;
        };

        EchoConnector& owner_;
        Endpoint target_;
        std::shared_ptr<State> state_;
    };

    Executor& strand_;
};

struct TargetRig {
    explicit TargetRig(std::optional<JoinTarget> loaded = std::nullopt)
        : target(resolver, prober, rt.ops(), rt.events(), std::move(loaded),
                 [this](const std::optional<JoinTarget>& saved) { persisted.push_back(saved); }) {}

    [[nodiscard]] std::optional<ErasedOutcome> outcome(const Result<OpHandle>& handle) {
        REQUIRE(handle);
        return rt.ops().outcome(handle->id());
    }

    [[nodiscard]] CheckedAddress checked(const Result<OpHandle>& handle) {
        const auto result = outcome(handle);
        REQUIRE(result);
        const auto* completed = std::get_if<Completed<std::any>>(&*result);
        REQUIRE(completed);
        return std::any_cast<CheckedAddress>(completed->value);
    }

    testing::DeterministicRuntime rt;
    testing::FakeResolver dns{rt.strand()};
    net::AddressResolver resolver{dns, rt.strand(), rt.timers()};
    EchoConnector connector{rt.strand()};
    net::UdpBeaconProber prober{connector, rt.strand(), rt.timers(), rt.clock()};
    std::vector<std::optional<JoinTarget>> persisted;
    GameServerTarget target;
};

}  // namespace

TEST_CASE("game server addresses parse with the default port", "[browser][target]") {
    const auto plain = parse_game_server_address("  play.test ");
    REQUIRE(plain);
    CHECK(plain->host == "play.test");
    CHECK(plain->port == Port{7777});
    CHECK(parse_game_server_address("10.0.0.1:7778")->port == Port{7778});
    const auto v6 = parse_game_server_address("[::1]:9000");
    REQUIRE(v6);
    CHECK(v6->host == "::1");
    CHECK(v6->port == Port{9000});
    CHECK(parse_game_server_address("").error().id == "net.address_invalid");
    CHECK(parse_game_server_address("host:port").error().id == "net.address_invalid");
    CHECK(parse_game_server_address("host:0").error().id == "net.address_invalid");
}

TEST_CASE("loopback and 0.0.0.0 are local whatever the port", "[browser][target]") {
    CHECK(is_local_host(Endpoint{ipv4(127, 0, 0, 1), Port{7778}}));
    CHECK(is_local_host(Endpoint{ipv4(127, 1, 2, 3), Port{7777}}));
    CHECK(is_local_host(Endpoint{ipv4(0, 0, 0, 0), Port{7777}}));
    CHECK(is_local_host(Endpoint{*IpAddress::parse("::1"), Port{7777}}));
    CHECK_FALSE(is_local_host(Endpoint{ipv4(10, 0, 0, 1), Port{7777}}));
}

TEST_CASE("a custom address is stored once it resolves and then probed", "[browser][target]") {
    TargetRig rig;
    testing::EventRecorder recorder(rig.rt.events(), EventFilter{{EventKind::JoinTargetChanged}, {}, {}});
    rig.dns.set("play.test", {ipv4(10, 0, 0, 5)});
    rig.connector.alive.insert(Endpoint{ipv4(10, 0, 0, 5), Port{7778}});
    const Result<OpHandle> handle = rig.target.start_set_custom(" play.test:7778 ", DisconnectPolicy::Detached);
    rig.rt.run_until_idle();
    const CheckedAddress checked = rig.checked(handle);
    CHECK(checked.endpoint == Endpoint{ipv4(10, 0, 0, 5), Port{7778}});
    CHECK(checked.probe == ProbeVerdict::Reachable);
    CHECK_FALSE(checked.warning);

    REQUIRE(rig.target.current());
    const auto* address = std::get_if<AddressTarget>(&rig.target.current()->target);
    REQUIRE(address);
    CHECK(address->text == "play.test:7778");
    CHECK(address->address == HostPort{"play.test", Port{7778}});
    REQUIRE(rig.persisted.size() == 1);
    CHECK(rig.persisted[0] == rig.target.current());
    recorder.pump();
    CHECK(recorder.count(EventKind::JoinTargetChanged) == 1);
}

TEST_CASE("a silent server is a warning, never a block", "[browser][target]") {
    TargetRig rig;
    rig.dns.set("quiet.test", {ipv4(10, 0, 0, 6)});
    const Result<OpHandle> handle = rig.target.start_set_custom("quiet.test", DisconnectPolicy::Detached);
    rig.rt.advance(kCheckAddressDeadline - 1s);
    const CheckedAddress checked = rig.checked(handle);
    CHECK(checked.probe == ProbeVerdict::Unreachable);
    REQUIRE(checked.warning);
    CHECK(checked.warning->severity == Severity::Warning);
    CHECK(checked.warning->id == "browser.target_unreachable");
    CHECK(rig.target.current().has_value());
    CHECK_FALSE(rig.connector.probed.empty());
}

TEST_CASE("a local address is not probed", "[browser][target]") {
    TargetRig rig;
    const Result<OpHandle> handle = rig.target.start_set_custom("127.0.0.1:7778", DisconnectPolicy::Detached);
    rig.rt.run_until_idle();
    CHECK(rig.checked(handle).probe == ProbeVerdict::Skipped);
    CHECK(rig.connector.probed.empty());
}

TEST_CASE("a name with only IPv6 addresses is refused and changes nothing", "[browser][target]") {
    TargetRig rig;
    rig.dns.set("six.test", {*IpAddress::parse("2001:db8::1")});
    const Result<OpHandle> handle = rig.target.start_set_custom("six.test", DisconnectPolicy::Detached);
    rig.rt.run_until_idle();
    const auto result = rig.outcome(handle);
    REQUIRE(result);
    const auto* failed = std::get_if<Failed>(&*result);
    REQUIRE(failed);
    CHECK(failed->error.id == "browser.unsupported_address_family");
    CHECK_FALSE(rig.target.current());
    CHECK(rig.persisted.empty());
}

TEST_CASE("an invalid address fails synchronously and creates no op", "[browser][target]") {
    TargetRig rig;
    const Result<OpHandle> handle = rig.target.start_set_custom("[::1", DisconnectPolicy::Detached);
    REQUIRE_FALSE(handle);
    CHECK(handle.error().id == "net.address_invalid");
    CHECK(rig.rt.ops().live().empty());
}

TEST_CASE("the latest call wins over a slower lookup", "[browser][target][race]") {
    TargetRig rig;
    rig.dns.hang("slow.test");
    const Result<OpHandle> slow = rig.target.start_set_custom("slow.test", DisconnectPolicy::Detached);
    rig.rt.run_until_idle();
    const ServerTarget picked{server_id(1), "Picked", "someone"};
    rig.target.set_server(picked);
    rig.rt.run_until_idle();
    const auto result = rig.outcome(slow);
    REQUIRE(result);
    REQUIRE(std::holds_alternative<Cancelled>(*result));
    CHECK(std::get<Cancelled>(*result).reason == CancelReason::Superseded);
    REQUIRE(rig.target.current());
    CHECK(std::get<ServerTarget>(rig.target.current()->target) == picked);

    rig.target.clear();
    CHECK_FALSE(rig.target.current());
    REQUIRE(rig.persisted.size() == 2);
    CHECK_FALSE(rig.persisted.back());
}

TEST_CASE("Play's check resolves and probes without touching the target", "[browser][target]") {
    const JoinTarget loaded{ServerTarget{server_id(2), "Saved", "me"}};
    TargetRig rig(loaded);
    rig.dns.set("play.test", {ipv4(10, 0, 0, 5)});
    rig.connector.alive.insert(Endpoint{ipv4(10, 0, 0, 5), Port{7777}});
    auto [handle, op] = rig.rt.ops().create<int>(OpKind::Play, DisconnectPolicy::Detached, std::nullopt);
    Capture<Result<CheckedAddress>> done;
    REQUIRE(rig.target.resolve(HostPort{"play.test", std::nullopt}, op, done.callback()));
    rig.rt.run_until_idle();
    REQUIRE(done.calls == 1);
    CHECK((*done.result)->probe == ProbeVerdict::Reachable);
    CHECK(rig.target.current() == loaded);
    CHECK(rig.persisted.empty());
}
