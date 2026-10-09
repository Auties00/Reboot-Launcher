#pragma once

#include <memory>

#include "reboot/engine/engine_command_line.hpp"
#include "reboot/ports/ipc.hpp"
#include "reboot/ports/platform_services.hpp"

namespace reboot::engine {

// Capabilities: os-integration.single-instance. The engine process; a second one for the same
// data root exits 0. state/spawn.lock belongs to the clients, and the engine never takes it.
class EngineHost {
public:
    // `platform.ipc_listener` is required. `self` is the user the endpoint is named after.
    // `self_test` reaches the engine's own endpoint for a pending update's self-test; without
    // one that self-test fails, and the update is retried or rolled back.
    EngineHost(EngineCommandLine command_line, ports::PlatformServices platform, ports::PeerIdentity self,
               std::unique_ptr<ports::IIpcConnector> self_test);
    ~EngineHost();
    EngineHost(const EngineHost&) = delete;
    EngineHost& operator=(const EngineHost&) = delete;

    // Blocks until the engine ends. Returns EngineExit, or exit_code_for() of a startup failure.
    // The origin becomes resumed_origin(command line, resume.json) before any service exists.
    [[nodiscard]] int run();

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace reboot::engine
