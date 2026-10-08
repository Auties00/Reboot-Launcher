#pragma once

#include <memory>
#include <string>
#include <string_view>

#include "reboot/foundation/diag.hpp"
#include "reboot/foundation/net_types.hpp"
#include "reboot/foundation/operation.hpp"
#include "reboot/foundation/types.hpp"
#include "reboot/game_channel/client_dll_peer.hpp"
#include "reboot/game_channel/winhost_peer.hpp"

namespace boost::asio {
class io_context;
}

namespace reboot {
class Executor;
class TimerService;
}  // namespace reboot

namespace reboot::ports {
class IByteStream;
}

namespace reboot::game_channel {

class TokenRegistry;

inline constexpr std::string_view kWinhostModule = "reboot-winhost.exe";

// Covers no capability ids (decisions game-control-channel, client-dll-design, testing-strategy, owner-6).
// The engine end of the loopback game-control channel. Sockets live on the I/O thread and only
// post decoded frames; everything else is strand-only. A connection must send the preamble and a
// Hello within TokenRegistry::hello_deadline(); a duplicate of a claimed token is logged and
// closed, and the session that owns the token is not told.
class GameChannelListener {
public:
    GameChannelListener(boost::asio::io_context& io, Executor& strand, TimerService& timers, TokenRegistry& tokens);
    // Peers must already be gone.
    ~GameChannelListener();
    GameChannelListener(const GameChannelListener&) = delete;
    GameChannelListener& operator=(const GameChannelListener&) = delete;

    // The IPv4 loopback literal on an OS-assigned port, since Wine maps 127.0.0.1.
    // game_channel.listen_failed.
    Result<Port> start();
    // Stops accepting and closes every connection.
    void close();

    // REBOOT_CTL, tcp://127.0.0.1:<port>; game_channel.not_listening before start().
    [[nodiscard]] Result<std::string> ctl_url() const;

    // game_channel.duplicate_peer when the key already has a live token.
    Result<std::unique_ptr<ClientDllPeer>> open_client_dll(SessionId session, std::string module,
                                                           RunnerMultiplier multiplier, ClientDllHandlers handlers);
    // Winhost shares this listener instead of a stdio channel, so its own token sits in the Wine
    // process's Unix environment; the game's bootstrap still travels only inside SpawnGame.
    Result<std::unique_ptr<WinhostPeer>> open_winhost(SessionId session, RunnerMultiplier multiplier,
                                                      WinhostHandlers handlers);

    // Serves an already connected stream as if it had been accepted.
    void adopt(std::unique_ptr<ports::IByteStream> stream);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace reboot::game_channel
