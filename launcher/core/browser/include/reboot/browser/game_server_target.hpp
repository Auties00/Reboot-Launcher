#pragma once

#include <chrono>
#include <memory>
#include <optional>
#include <string_view>

#include "reboot/browser/join_target.hpp"
#include "reboot/foundation/diag.hpp"
#include "reboot/foundation/function.hpp"
#include "reboot/foundation/net_types.hpp"
#include "reboot/foundation/operation.hpp"
#include "reboot/foundation/types.hpp"

namespace rb {
class EventBus;
}

namespace rb::net {
class AddressResolver;
class UdpBeaconProber;
}  // namespace rb::net

namespace rb::browser {

// Loopback or 0.0.0.0, compared on the address only, so "127.0.0.1:7778" is local too.
[[nodiscard]] bool is_local_host(const Endpoint& endpoint) noexcept;

// "h", "h:p" or "[v6]:p", trimmed, with port 7777 when absent. Fails with net.address_invalid.
[[nodiscard]] Result<HostPort> parse_game_server_address(std::string_view text);

// Skipped: a local endpoint, whose server usually starts with the game.
enum class ProbeVerdict : u8 { Reachable, Unreachable, Skipped };

struct CheckedAddress {
    // The first IPv4 result.
    Endpoint endpoint;
    ProbeVerdict probe = ProbeVerdict::Skipped;
    // browser.target_unreachable as a Warning: private and LAN servers often ignore the probe.
    std::optional<Diagnostic> warning;
};

// DNS within its 5 s bound, then three 2 s probe attempts one second apart, with slack.
inline constexpr std::chrono::milliseconds kCheckAddressDeadline{15'000};

// Capabilities: server-browser.join-by-address, server-browser.stale-address-check, server-browser.+50, matchmaking-networking.+33.
// Strand-only. Owns the persisted JoinTarget Play falls back to; the latest set_*/clear call wins.
// Only an IPv4 address reaches the game; a probe failure is a warning, never a block.
class GameServerTarget {
public:
    using Persist = UniqueFunction<void(const std::optional<JoinTarget>&)>;

    GameServerTarget(net::AddressResolver& resolver, net::UdpBeaconProber& prober, OpRegistry& ops, EventBus& events,
                     std::optional<JoinTarget> loaded, Persist persist);
    ~GameServerTarget();
    GameServerTarget(const GameServerTarget&) = delete;
    GameServerTarget& operator=(const GameServerTarget&) = delete;

    [[nodiscard]] const std::optional<JoinTarget>& current() const noexcept { return current_; }

    // An Operation<CheckedAddress> of OpKind::Generic bounded by kCheckAddressDeadline. Validates
    // synchronously; the target is stored once the name resolves, whatever the probe says. A name
    // with only IPv6 addresses fails with browser.unsupported_address_family.
    [[nodiscard]] Result<OpHandle> start_set_custom(std::string_view text, DisconnectPolicy policy);
    // A server the user picked or confirmed from a link.
    void set_server(ServerTarget server);
    void clear();

    // For Play, inside its op: resolves and probes `address` again before every launch, under the
    // op's token. `done` runs on the strand exactly once.
    [[nodiscard]] Result<void> resolve(const HostPort& address, OperationBase& op,
                                       UniqueFunction<void(Result<CheckedAddress>)> done);

private:
    // Cancels a pending start_set_custom with Superseded, so a slower lookup cannot land last.
    void supersede_pending();
    void adopt(std::optional<JoinTarget> target);
    // Resolves to the first IPv4 address, hands it to `resolved`, then probes it unless local.
    [[nodiscard]] Result<void> check(const HostPort& address, CancelToken token,
                                     UniqueFunction<void(const Endpoint&)> resolved,
                                     UniqueFunction<void(Result<CheckedAddress>)> done);

    net::AddressResolver& resolver_;
    net::UdpBeaconProber& prober_;
    OpRegistry& ops_;
    EventBus& events_;
    Persist persist_;
    std::optional<JoinTarget> current_;
    std::optional<OpId> pending_set_;
    // Cleared on destruction; resolver and prober callbacks that arrive later do nothing.
    std::shared_ptr<GameServerTarget*> self_;
};

}  // namespace rb::browser
