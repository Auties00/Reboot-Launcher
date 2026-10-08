#pragma once

#include <cstddef>
#include <span>
#include <string_view>
#include <vector>

#include "reboot/foundation/net_types.hpp"
#include "reboot/foundation/types.hpp"
#include "reboot/front/upstream_origin.hpp"

namespace reboot::front {

// Hotfix and ticket bodies above this are relayed without being read.
inline constexpr std::size_t kLearnBodyCap = 1u << 20;

// Capabilities: auth-backend.reverse-proxy, auth-backend.+12.
// One Local or Remote route's upstream allow-list: the backend origin plus origins learned from it. Strand-only.
class UpstreamPolicy {
public:
    explicit UpstreamPolicy(UpstreamOrigin backend);

    [[nodiscard]] const UpstreamOrigin& backend() const noexcept { return backend_; }

    // The backend or a learned origin, matching scheme, host and port.
    [[nodiscard]] bool allows(const UpstreamOrigin& target) const;

    // After DNS, before every connect: allowed, never a front port, and loopback only for a loopback backend.
    [[nodiscard]] bool allows_connect(const UpstreamOrigin& target, const Endpoint& resolved,
                                      std::span<const Port> front_ports) const;

    // XMPP ServerAddr from a cloudstorage system file, serviceUrl from a matchmaking ticket; returns the new ones.
    std::vector<UpstreamOrigin> learn(std::string_view path, std::span<const u8> decoded_body);

    [[nodiscard]] const std::vector<UpstreamOrigin>& learned() const noexcept { return learned_; }

private:
    UpstreamOrigin backend_;
    bool backend_on_loopback_;
    std::vector<UpstreamOrigin> learned_;
};

}  // namespace reboot::front
