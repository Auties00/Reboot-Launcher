#pragma once

#include <memory>
#include <optional>
#include <vector>

#include "reboot/contracts/winhost.hpp"
#include "reboot/foundation/clock.hpp"
#include "reboot/foundation/diag.hpp"
#include "reboot/foundation/executor.hpp"
#include "reboot/foundation/framing.hpp"
#include "reboot/ports/ipc.hpp"
#include "reboot/ports/process.hpp"
#include "reboot/testing/fake_winhost_script.hpp"
#include "reboot/testing/frame_log.hpp"
#include "reboot/testing/game_control_bootstrap.hpp"

namespace boost::asio {
class io_context;
}

namespace rb::testing {

class FakeClientDll;

// Covers no capability ids (decisions testing-strategy, game-control-channel, process-model).
// Plays reboot-winhost.exe inside a Wine prefix against the engine's game channel; SpawnGame is
// recorded instead of run. EOF from the engine marks the Job terminated, as the kill chain expects.
class FakeWinhost {
public:
    FakeWinhost(Executor& executor, const IClock& clock, FakeWinhostScript script);
    ~FakeWinhost();
    FakeWinhost(const FakeWinhost&) = delete;
    FakeWinhost& operator=(const FakeWinhost&) = delete;

    Result<void> connect(boost::asio::io_context& io, const GameControlBootstrap& bootstrap);
    void attach(std::unique_ptr<ports::IByteStream> stream, const GameControlBootstrap& bootstrap);
    // The runner launch a ScriptedProcessLauncher rule received carries winhost's bootstrap.
    [[nodiscard]] static Result<GameControlBootstrap> bootstrap_of(const ports::ProcessLaunch& runner_launch);

    template <ContractMessage T>
    void send(const T& message) {
        send_frame(encode_contract_frame(message));
    }
    void game_exits(i64 code);
    void disconnect();

    [[nodiscard]] std::optional<contracts::winhost::SpawnGame> spawn_request() const;
    [[nodiscard]] bool resumed() const;
    [[nodiscard]] std::vector<contracts::winhost::InjectSpec> injected() const;
    [[nodiscard]] std::optional<u32> stop_grace_ms() const;
    [[nodiscard]] bool job_terminated() const;
    [[nodiscard]] std::vector<OwnedFrame> received_frames() const;
    // The DLL the script's game_client_dll started, once Resume arrived. After connect() it
    // reaches the engine over TCP by itself; after attach() the test attaches its stream.
    [[nodiscard]] FakeClientDll* game_client_dll() const;

private:
    void send_frame(std::vector<u8> frame);

    struct Impl;
    // Shared with socket handlers and posted steps, which may outlive this object.
    std::shared_ptr<Impl> impl_;
};

}  // namespace rb::testing
