#include <chrono>
#include <memory>
#include <optional>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "reboot/contracts/game_server.hpp"
#include "reboot/foundation/cancel.hpp"
#include "reboot/net/datagram_connector.hpp"
#include "reboot/net/udp_beacon_prober.hpp"
#include "reboot/testing/deterministic_runtime.hpp"

using namespace reboot;
using namespace reboot::net;
using namespace std::chrono_literals;

namespace {

// Channels whose sends are recorded; the test answers or refuses through the latest one.
class FakeConnector final : public IDatagramConnector {
public:
    struct Channel {
        DatagramCallbacks callbacks;
        std::vector<SteadyTime> sends;
        bool open = true;
    };

    explicit FakeConnector(ManualClock& clock) : clock_(clock) {}

    Result<std::unique_ptr<IDatagramChannel>> connect(Endpoint target, DatagramCallbacks callbacks) override {
        if (fail_connect) return make_diag(ErrorDomain::Net, MessageId{"net.udp_socket_failed"}).fail();
        targets.push_back(target);
        auto channel = std::make_shared<Channel>();
        channel->callbacks = std::move(callbacks);
        channels.push_back(channel);
        return std::make_unique<Handle>(channel, clock_);
    }

    void reply() { channels.back()->callbacks.on_datagram(std::span<const u8>(contracts::game_server::kRbsbProbe)); }
    void refuse() { channels.back()->callbacks.on_failure(DatagramFailure::Refused, SystemError{SystemError::Origin::Host, 111}); }

    std::vector<std::shared_ptr<Channel>> channels;
    std::vector<Endpoint> targets;
    bool fail_connect = false;

private:
    class Handle final : public IDatagramChannel {
    public:
        Handle(std::shared_ptr<Channel> channel, ManualClock& clock) : channel_(std::move(channel)), clock_(clock) {}
        ~Handle() override { channel_->open = false; }
        void send(std::span<const u8> bytes) override {
            CHECK(std::ranges::equal(bytes, contracts::game_server::kRbsbProbe));
            channel_->sends.push_back(clock_.steady_now());
        }

    private:
        std::shared_ptr<Channel> channel_;
        ManualClock& clock_;
    };

    ManualClock& clock_;
};

struct Fixture {
    testing::DeterministicRuntime runtime;
    FakeConnector connector{runtime.clock()};
    UdpBeaconProber prober{connector, runtime.strand(), runtime.timers(), runtime.clock()};
    std::optional<ProbeResult> result;
    int calls = 0;

    Result<void> probe(ProbePolicy policy = {}, CancelToken token = {}, Endpoint target = Endpoint{IpAddress::v4(0x7F000001), Port{7777}}) {
        return prober.probe(target, policy, std::move(token), [this](ProbeResult answer) {
            ++calls;
            result = answer;
        });
    }
};

}  // namespace

TEST_CASE("a reply is Alive with the RTT from the last send", "[net][probe]") {
    Fixture f;
    REQUIRE(f.probe());
    f.runtime.run_until_idle();
    REQUIRE(f.connector.channels.size() == 1);
    CHECK(f.connector.channels.front()->sends.size() == 1);
    f.runtime.advance(350ms);
    CHECK(f.connector.channels.front()->sends.size() == 2);
    f.runtime.advance(20ms);
    f.connector.reply();
    f.runtime.run_until_idle();
    REQUIRE(f.calls == 1);
    CHECK(f.result->outcome == ProbeOutcome::Alive);
    CHECK(f.result->attempts_used == 1);
    REQUIRE(f.result->rtt);
    CHECK(*f.result->rtt == 70ms);
    CHECK_FALSE(f.connector.channels.front()->open);
}

TEST_CASE("each attempt resends on schedule, then the next waits the interval", "[net][probe]") {
    Fixture f;
    REQUIRE(f.probe());
    f.runtime.advance(2s);
    REQUIRE(f.connector.channels.size() == 1);
    const std::vector<SteadyTime>& sends = f.connector.channels.front()->sends;
    REQUIRE(sends.size() == 3);
    CHECK(sends[1] - sends[0] == 300ms);
    CHECK(sends[2] - sends[0] == 1000ms);
    CHECK_FALSE(f.connector.channels.front()->open);
    f.runtime.advance(999ms);
    CHECK(f.connector.channels.size() == 1);
    f.runtime.advance(1ms);
    CHECK(f.connector.channels.size() == 2);
    f.runtime.advance(10s);
    REQUIRE(f.calls == 1);
    CHECK(f.result->outcome == ProbeOutcome::TimedOut);
    CHECK(f.result->attempts_used == 3);
    CHECK_FALSE(f.result->rtt);
}

TEST_CASE("a refusal ends the attempt early; a refused last attempt is Refused", "[net][probe]") {
    Fixture f;
    ProbePolicy policy;
    policy.attempts = 2;
    REQUIRE(f.probe(policy));
    f.runtime.run_until_idle();
    f.connector.refuse();
    f.runtime.run_until_idle();
    CHECK_FALSE(f.connector.channels.front()->open);
    f.runtime.advance(1s);
    REQUIRE(f.connector.channels.size() == 2);
    f.connector.refuse();
    f.runtime.run_until_idle();
    REQUIRE(f.calls == 1);
    CHECK(f.result->outcome == ProbeOutcome::Refused);
    CHECK(f.result->attempts_used == 2);

    Fixture recovered;
    REQUIRE(recovered.probe(policy));
    recovered.runtime.run_until_idle();
    recovered.connector.refuse();
    recovered.runtime.advance(1s);
    recovered.connector.reply();
    recovered.runtime.run_until_idle();
    CHECK(recovered.result->outcome == ProbeOutcome::Alive);
    CHECK(recovered.result->attempts_used == 2);
}

TEST_CASE("late callbacks of a closed attempt are ignored", "[net][probe][race]") {
    Fixture f;
    ProbePolicy policy;
    policy.attempts = 2;
    REQUIRE(f.probe(policy));
    f.runtime.advance(2s);
    const std::shared_ptr<FakeConnector::Channel> first = f.connector.channels.front();
    first->callbacks.on_datagram(std::span<const u8>(contracts::game_server::kRbsbProbe));
    f.runtime.run_until_idle();
    CHECK(f.calls == 0);
    f.runtime.advance(1s);
    f.connector.reply();
    f.runtime.run_until_idle();
    CHECK(f.calls == 1);
    CHECK(f.result->attempts_used == 2);
}

TEST_CASE("cancelling yields Cancelled once, and bad policies fail synchronously", "[net][probe][race]") {
    Fixture f;
    CancelSource source;
    REQUIRE(f.probe({}, source.token()));
    f.runtime.advance(500ms);
    source.cancel(CancelReason::User);
    f.connector.reply();
    f.runtime.advance(10s);
    REQUIRE(f.calls == 1);
    CHECK(f.result->outcome == ProbeOutcome::Cancelled);

    Fixture bad;
    CHECK(bad.probe({}, {}, Endpoint{IpAddress::v4(1), Port{0}}).error().id == "net.probe_policy_invalid");
    ProbePolicy no_attempts;
    no_attempts.attempts = 0;
    CHECK_FALSE(bad.probe(no_attempts));
    ProbePolicy no_timeout;
    no_timeout.attempt_timeout = 0ms;
    CHECK_FALSE(bad.probe(no_timeout));
}

TEST_CASE("a channel that cannot open uses up attempts and times out", "[net][probe]") {
    Fixture f;
    f.connector.fail_connect = true;
    ProbePolicy policy;
    policy.attempts = 2;
    REQUIRE(f.probe(policy));
    f.runtime.advance(5s);
    REQUIRE(f.calls == 1);
    CHECK(f.result->outcome == ProbeOutcome::TimedOut);
    CHECK(f.result->attempts_used == 2);
}
