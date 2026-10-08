#pragma once

#include <chrono>
#include <memory>
#include <optional>
#include <span>

#include "reboot/contracts/game_client.hpp"
#include "reboot/foundation/diag.hpp"
#include "reboot/foundation/operation.hpp"
#include "reboot/game_channel/control_token.hpp"
#include "reboot/game_channel/peer_key.hpp"

namespace reboot {
class IRandom;
class Redactor;
}  // namespace reboot

namespace reboot::game_channel {

// Covers no capability ids (decision game-control-channel).
// Strand-only. Mints tokens and routes a presented token to its key. Each token's
// REBOOT_CTL_TOKEN text stays registered with the Redactor until it is revoked.
class TokenRegistry {
public:
    TokenRegistry(IRandom& random, Redactor& redactor);
    ~TokenRegistry();
    TokenRegistry(const TokenRegistry&) = delete;
    TokenRegistry& operator=(const TokenRegistry&) = delete;

    // game_channel.duplicate_peer when the key already has a live token.
    Result<ControlToken> issue(PeerKey key, RunnerMultiplier multiplier);

    // Compares against every live token, so timing does not reveal which one matched.
    // game_channel.unknown_token, game_channel.role_mismatch, or game_channel.duplicate_peer once
    // the token has been claimed.
    Result<PeerKey> claim(std::span<const u8, kControlTokenSize> token, contracts::game_client::PeerRole role);

    // A later Hello with the key's token is unknown_token.
    void revoke(const PeerKey& key);

    // 2 s times the largest multiplier among unclaimed tokens; nullopt when none is waiting.
    [[nodiscard]] std::optional<std::chrono::milliseconds> hello_deadline() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace reboot::game_channel
