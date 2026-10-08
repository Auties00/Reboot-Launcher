#pragma once

#include <memory>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include "reboot/contracts/game_server.hpp"
#include "reboot/foundation/clock.hpp"
#include "reboot/foundation/executor.hpp"
#include "reboot/foundation/function.hpp"
#include "reboot/foundation/types.hpp"
#include "reboot/testing/fake_game_server_script.hpp"
#include "reboot/testing/stdio_peer.hpp"

namespace boost::asio {
class io_context;
}

namespace reboot::testing {

// Covers no capability ids (decisions testing-strategy, game-server-dll-design, concurrent-hosts).
// The program side of contracts/game_server.hpp; unscripted it is a conforming game server that
// binds exactly the Welcome ports and never blocks writing stdout.
class FakeGameServer final : public IStdioPeer {
public:
    // The script must not bind sockets.
    FakeGameServer(Executor& executor, const IClock& clock, FakeGameServerScript script);
    // Binds the script's sockets on `socket_io`.
    FakeGameServer(Executor& executor, const IClock& clock, FakeGameServerScript script,
                   boost::asio::io_context& socket_io);
    ~FakeGameServer() override;
    FakeGameServer(const FakeGameServer&) = delete;
    FakeGameServer& operator=(const FakeGameServer&) = delete;

    void start(StdioPeerOutputs outputs) override;
    void on_stdin(std::span<const u8> bytes) override;
    void on_stdin_eof() override;

    // In-process, e.g. to mark the ports owned in a FakePortInspector.
    void on_listening(UniqueFunction<void(const contracts::game_server::Listening&)> hook);

    [[nodiscard]] std::optional<contracts::game_server::ServerConfig> config() const;
    [[nodiscard]] std::optional<contracts::game_server::MatchState> state() const;
    [[nodiscard]] std::vector<contracts::game_server::Ban> bans() const;
    [[nodiscard]] std::vector<std::string> operator_cidrs() const;
    [[nodiscard]] std::vector<std::string> commands() const;
    [[nodiscard]] std::vector<u32> kicked() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

// The one frame `--describe` writes to stdout.
[[nodiscard]] std::vector<u8> describe_frame(const FakeGameServerScript& script);

// reboot-fake-game-server `--describe | --control=stdio [--script=<file>]`; the script is found as
// fake_backend_main finds its own.
int fake_game_server_main(int argc, char** argv);

}  // namespace reboot::testing
