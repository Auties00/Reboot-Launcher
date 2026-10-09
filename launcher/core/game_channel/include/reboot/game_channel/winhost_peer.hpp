#pragma once

#include <chrono>
#include <memory>
#include <string>
#include <variant>

#include "reboot/contracts/winhost.hpp"
#include "reboot/foundation/diag.hpp"
#include "reboot/foundation/function.hpp"
#include "reboot/foundation/types.hpp"
#include "reboot/game_channel/control_token.hpp"
#include "reboot/game_channel/peer_liveness.hpp"
#include "reboot/game_channel/reply_handler.hpp"

namespace rb::game_channel {

// What winhost said in its Hello, without the token.
struct WinhostHello {
    std::string build;
    u32 pid = 0;
};

using WinhostEvent = std::variant<contracts::winhost::Spawned, contracts::winhost::Injected, contracts::winhost::Output,
                                  contracts::winhost::Exited, contracts::winhost::WhFatal>;

// Every callback runs on the strand.
struct WinhostHandlers {
    // The SpawnGame that goes in the Welcome; an error refuses the peer.
    UniqueFunction<Result<contracts::winhost::SpawnGame>(const WinhostHello&)> configure;
    UniqueFunction<void(WinhostEvent)> on_event;
    UniqueFunction<void(PeerLiveness)> on_liveness;
    // Fatal to the session, at most once: the connection that claimed the token was refused
    // before Welcome, or it ended after Welcome (game_channel.peer_lost).
    UniqueFunction<void(Diagnostic)> on_lost;
};

// Covers no capability ids (decisions game-control-channel, testing-strategy).
// Strand-only. compat's handle on reboot-winhost.exe inside the Wine prefix. Dropping it revokes
// the token and closes the connection without calling on_lost; winhost then terminates its Job.
class WinhostPeer {
public:
    ~WinhostPeer();
    WinhostPeer(const WinhostPeer&) = delete;
    WinhostPeer& operator=(const WinhostPeer&) = delete;

    [[nodiscard]] const ControlToken& token() const noexcept;
    [[nodiscard]] bool welcomed() const noexcept;

    // Each fails at once with game_channel.not_welcomed before Welcome. Otherwise `done` gets
    // winhost's CommandResult, game_channel.unsupported_request, or game_channel.peer_lost.
    Result<void> resume(ReplyHandler done);
    Result<void> inject(contracts::winhost::InjectSpec entry, ReplyHandler done);
    Result<void> stop(std::chrono::milliseconds grace, ReplyHandler done);

private:
    friend class GameChannelListener;
    struct Impl;
    explicit WinhostPeer(std::unique_ptr<Impl> impl);

    std::unique_ptr<Impl> impl_;
};

}  // namespace rb::game_channel
