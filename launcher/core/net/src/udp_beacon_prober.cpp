#include "reboot/net/udp_beacon_prober.hpp"

#include <map>
#include <memory>
#include <utility>

#include "messages.hpp"
#include "reboot/contracts/game_server.hpp"
#include "reboot/foundation/clock.hpp"
#include "reboot/foundation/executor.hpp"
#include "reboot/net/datagram_connector.hpp"

namespace rb::net {

struct UdpBeaconProber::Impl {
    struct Probe {
        Endpoint target;
        ProbePolicy policy;
        UniqueFunction<void(ProbeResult)> done;
        CancelRegistration user_cancel;
        std::unique_ptr<IDatagramChannel> channel;
        std::vector<TimerHandle> resends;
        TimerHandle attempt_end;
        TimerHandle next_attempt;
        u32 attempt = 0;
        SteadyTime last_send{};
        bool last_refused = false;
    };

    struct Core : std::enable_shared_from_this<Core> {
        Core(IDatagramConnector& connector_in, Executor& strand_in, TimerService& timers_in, IClock& clock_in)
            : connector(connector_in), strand(strand_in), timers(timers_in), clock(clock_in) {}

        // Channel callbacks name their attempt and need its channel open, so a late one is ignored.
        void start_attempt(u64 id) {
            Probe& probe = probes.at(id);
            ++probe.attempt;
            probe.last_refused = false;
            const u32 attempt = probe.attempt;
            const std::weak_ptr<Core> weak = weak_from_this();
            DatagramCallbacks callbacks;
            callbacks.on_datagram = [weak, id, attempt](std::span<const u8>) {
                post(weak, [id, attempt](Core& self) { self.replied(id, attempt); });
            };
            callbacks.on_failure = [weak, id, attempt](DatagramFailure failure, std::optional<SystemError>) {
                post(weak, [id, attempt, failure](Core& self) { self.failed(id, attempt, failure); });
            };
            Result<std::unique_ptr<IDatagramChannel>> channel = connector.connect(probe.target, std::move(callbacks));
            if (!channel) {
                end_attempt(id, attempt);
                return;
            }
            probe.channel = std::move(*channel);
            probe.resends.clear();
            for (const std::chrono::milliseconds offset : probe.policy.resend_at) {
                if (offset >= probe.policy.attempt_timeout) continue;
                probe.resends.push_back(timers.after(offset, [weak, id, attempt] {
                    if (const std::shared_ptr<Core> self = weak.lock()) self->send(id, attempt);
                }));
            }
            probe.attempt_end = timers.after(probe.policy.attempt_timeout, [weak, id, attempt] {
                if (const std::shared_ptr<Core> self = weak.lock()) self->end_attempt(id, attempt);
            });
        }

        void send(u64 id, u32 attempt) {
            const auto it = probes.find(id);
            if (it == probes.end() || it->second.attempt != attempt || !it->second.channel) return;
            it->second.last_send = clock.steady_now();
            it->second.channel->send(contracts::game_server::kRbsbProbe);
        }

        void replied(u64 id, u32 attempt) {
            const auto it = probes.find(id);
            if (it == probes.end() || it->second.attempt != attempt || !it->second.channel) return;
            const auto rtt = std::chrono::duration_cast<std::chrono::milliseconds>(clock.steady_now() - it->second.last_send);
            finish(id, ProbeOutcome::Alive, rtt);
        }

        void failed(u64 id, u32 attempt, DatagramFailure failure) {
            const auto it = probes.find(id);
            if (it == probes.end() || it->second.attempt != attempt || !it->second.channel) return;
            it->second.last_refused = failure == DatagramFailure::Refused;
            end_attempt(id, attempt);
        }

        void end_attempt(u64 id, u32 attempt) {
            const auto it = probes.find(id);
            if (it == probes.end() || it->second.attempt != attempt) return;
            Probe& probe = it->second;
            probe.channel.reset();
            probe.resends.clear();
            probe.attempt_end.cancel();
            if (probe.attempt >= probe.policy.attempts) {
                finish(id, probe.last_refused ? ProbeOutcome::Refused : ProbeOutcome::TimedOut, std::nullopt);
                return;
            }
            // With the channel gone, its late callbacks no longer count.
            probe.next_attempt = timers.after(probe.policy.interval, [weak = weak_from_this(), id] {
                if (const std::shared_ptr<Core> self = weak.lock(); self && self->probes.contains(id)) self->start_attempt(id);
            });
        }

        void finish(u64 id, ProbeOutcome outcome, std::optional<std::chrono::milliseconds> rtt) {
            const auto it = probes.find(id);
            if (it == probes.end()) return;
            Probe probe = std::move(it->second);
            probes.erase(it);
            probe.user_cancel.reset();
            probe.channel.reset();
            UniqueFunction<void(ProbeResult)> done = std::move(probe.done);
            done(ProbeResult{outcome, probe.target, probe.attempt, rtt});
        }

        template <class F>
        static void post(const std::weak_ptr<Core>& weak, F&& step) {
            const std::shared_ptr<Core> self = weak.lock();
            if (!self) return;
            self->strand.post([weak, step = std::forward<F>(step)]() mutable {
                if (const std::shared_ptr<Core> owner = weak.lock()) step(*owner);
            });
        }

        IDatagramConnector& connector;
        Executor& strand;
        TimerService& timers;
        IClock& clock;
        u64 next_id = 1;
        std::map<u64, Probe> probes;
    };

    Impl(IDatagramConnector& connector, Executor& strand, TimerService& timers, IClock& clock)
        : core(std::make_shared<Core>(connector, strand, timers, clock)) {}

    ~Impl() {
        for (auto& [id, probe] : core->probes) probe.user_cancel.reset();
        core->probes.clear();
    }

    std::shared_ptr<Core> core;
};

UdpBeaconProber::UdpBeaconProber(IDatagramConnector& connector, Executor& strand, TimerService& timers, IClock& clock)
    : impl_(std::make_unique<Impl>(connector, strand, timers, clock)) {}

UdpBeaconProber::~UdpBeaconProber() = default;

Result<void> UdpBeaconProber::probe(Endpoint target, ProbePolicy policy, CancelToken token,
                                    UniqueFunction<void(ProbeResult)> done) {
    if (target.port.value == 0 || policy.attempts == 0 || policy.attempt_timeout <= std::chrono::milliseconds::zero())
        return make_diag(ErrorDomain::Net, kProbePolicyInvalid).kind(ErrorKind::InvalidInput).fail();

    Impl::Core& core = *impl_->core;
    const u64 id = core.next_id++;
    Impl::Probe& probe = core.probes[id];
    probe.target = target;
    probe.policy = policy;
    probe.done = std::move(done);
    probe.user_cancel = token.on_cancel([weak = core.weak_from_this(), id](CancelReason) {
        Impl::Core::post(weak, [id](Impl::Core& self) { self.finish(id, ProbeOutcome::Cancelled, std::nullopt); });
    });
    Impl::Core::post(core.weak_from_this(), [id](Impl::Core& self) {
        if (self.probes.contains(id)) self.start_attempt(id);
    });
    return {};
}

}  // namespace rb::net
