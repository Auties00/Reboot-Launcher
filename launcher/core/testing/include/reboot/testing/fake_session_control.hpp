#pragma once

#include <chrono>
#include <memory>
#include <optional>
#include <vector>

#include "reboot/foundation/diag.hpp"
#include "reboot/foundation/executor.hpp"
#include "reboot/foundation/function.hpp"
#include "reboot/foundation/types.hpp"
#include "reboot/ports/session_host.hpp"
#include "reboot/testing/game_control_bootstrap.hpp"

namespace rb::testing {

class FakeSessionHost;

// Covers no capability ids (decision testing-strategy).
// The test's side of one session launched through FakeSessionHost. Events reach the engine through
// `io`, and nothing after it destroyed its IGameSession; inject and resume consult the host's faults.
// A successful inject() reports Injected; stop() records its grace and ends a running game with
// kJobKillExitCode, as the Job kill would.
class FakeSessionControl {
public:
    FakeSessionControl(FakeSessionHost& host, Executor& io, ports::SessionLaunch launch,
                       UniqueFunction<void(ports::SessionHostEvent)> on_event);
    ~FakeSessionControl();
    FakeSessionControl(const FakeSessionControl&) = delete;
    FakeSessionControl& operator=(const FakeSessionControl&) = delete;

    [[nodiscard]] std::unique_ptr<ports::IGameSession> make_handle();

    [[nodiscard]] const ports::SessionLaunch& launch() const noexcept { return launch_; }
    // Where our client DLL in this game would connect; an error when the launch carries no bootstrap.
    [[nodiscard]] Result<GameControlBootstrap> bootstrap() const;

    void emit(ports::SessionHostEvent event);
    // Spawned for the game and then each companion, with pids counting up from `first_pid`.
    void spawn_all(u32 first_pid = 0x2000);
    void game_exits(std::optional<int> code);

    [[nodiscard]] bool resumed() const noexcept { return resumed_; }
    [[nodiscard]] const std::vector<ports::InjectEntry>& injected() const noexcept { return injected_; }
    [[nodiscard]] std::optional<std::chrono::milliseconds> stop_grace() const noexcept { return stop_grace_; }
    [[nodiscard]] bool released() const noexcept { return released_; }

private:
    struct Delivery;

    FakeSessionHost& host_;
    Executor& io_;
    ports::SessionLaunch launch_;
    // Shared with the engine's handle and posted events, so a late event after release is dropped.
    std::shared_ptr<Delivery> delivery_;
    std::vector<ports::InjectEntry> injected_;
    std::optional<std::chrono::milliseconds> stop_grace_;
    bool resumed_ = false;
    bool released_ = false;
};

}  // namespace rb::testing
