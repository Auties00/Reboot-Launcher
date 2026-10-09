#pragma once

#include <array>
#include <memory>
#include <string>

#include "reboot/contracts/game_client.hpp"
#include "reboot/foundation/diag.hpp"
#include "reboot/foundation/function.hpp"
#include "reboot/foundation/types.hpp"
#include "reboot/game_channel/control_token.hpp"
#include "reboot/game_channel/game_lifecycle_event.hpp"
#include "reboot/game_channel/peer_liveness.hpp"
#include "reboot/game_channel/reply_handler.hpp"

namespace rb::game_channel {

// What a client DLL said in its Hello, without the token.
struct ClientDllHello {
    std::string dll_build;
    contracts::game_client::GameBuild game;
    std::array<u8, 32> exe_sha256{};
    u32 pid = 0;
};

// Every callback runs on the strand.
struct ClientDllHandlers {
    // Builds the Welcome once the Hello is accepted; an error refuses the peer.
    UniqueFunction<Result<contracts::game_client::ClientDllConfig>(const ClientDllHello&)> configure;
    UniqueFunction<void(GameLifecycleEvent)> on_event;
    // Unresponsive when Pongs stop or the game-thread tick they carry stops advancing.
    UniqueFunction<void(PeerLiveness)> on_liveness;
    // Fatal to the session, at most once: the connection that claimed the token was refused
    // before Welcome, or it ended after Welcome (game_channel.peer_lost).
    UniqueFunction<void(Diagnostic)> on_lost;
};

// Covers no capability ids (decisions game-control-channel, client-dll-design, testing-strategy).
// Strand-only. Our client DLL in one game process. Dropping it revokes the token and closes the
// connection without calling on_lost.
class ClientDllPeer {
public:
    ~ClientDllPeer();
    ClientDllPeer(const ClientDllPeer&) = delete;
    ClientDllPeer& operator=(const ClientDllPeer&) = delete;

    [[nodiscard]] const ControlToken& token() const noexcept;
    [[nodiscard]] bool welcomed() const noexcept;

    // Each fails at once with game_channel.not_welcomed before Welcome. Otherwise `done` gets the
    // DLL's CommandResult, game_channel.unsupported_request, or game_channel.peer_lost.
    Result<void> shutdown(ReplyHandler done);
    // Test mode only (game_channel.test_mode_off). The outcome arrives as Joined or Disconnected.
    Result<void> test_join(std::string address, ReplyHandler done);
    Result<void> test_quit(ReplyHandler done);

private:
    friend class GameChannelListener;
    struct Impl;
    explicit ClientDllPeer(std::unique_ptr<Impl> impl);

    std::unique_ptr<Impl> impl_;
};

}  // namespace rb::game_channel
