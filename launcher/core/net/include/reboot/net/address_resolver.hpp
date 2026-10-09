#pragma once

#include <memory>
#include <string_view>
#include <vector>

#include "reboot/foundation/cancel.hpp"
#include "reboot/foundation/diag.hpp"
#include "reboot/foundation/function.hpp"
#include "reboot/foundation/net_types.hpp"
#include "reboot/foundation/types.hpp"

namespace rb {
class Executor;
class TimerService;
}  // namespace rb

namespace rb::ports {
class IResolver;
}

namespace rb::net {

inline constexpr Port kDefaultGamePort{7777};

// Ipv4Only drops IPv6 results and fails with net.no_ipv4_address when nothing is left; the
// game and our hosts only speak IPv4.
enum class AddressFamilyPolicy : u8 { Any, Ipv4Only };

struct ResolvedAddress {
    HostPort parsed;
    // In resolver order; never empty.
    std::vector<Endpoint> endpoints;
};

// Capabilities: matchmaking-networking.udp-ping.
// Parses "h", "h:p" and "[v6]:p" (parse_host_port) and resolves names through IResolver within
// the Dns deadline. IP literals skip the resolver. "localhost", "0.0.0.0" and "127.0.0.1" all
// resolve to 127.0.0.1 without it, because a connected UDP socket to 0.0.0.0 fails on Windows.
// Failures are ResolveError diagnostics. Strand-only; callers resolve again before each launch
// rather than keep the result.
class AddressResolver {
public:
    AddressResolver(ports::IResolver& resolver, Executor& strand, TimerService& timers);
    ~AddressResolver();
    AddressResolver(const AddressResolver&) = delete;
    AddressResolver& operator=(const AddressResolver&) = delete;

    // Fails synchronously when the text does not parse. `done` runs on the strand exactly once.
    Result<void> resolve(std::string_view text, Port default_port, AddressFamilyPolicy family, CancelToken token,
                         UniqueFunction<void(Result<ResolvedAddress>)> done);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace rb::net
