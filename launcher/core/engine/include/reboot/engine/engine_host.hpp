#pragma once

#include <memory>

#include "reboot/engine/engine_command_line.hpp"
#include "reboot/ports/platform_services.hpp"

namespace reboot::engine {

// Capabilities: os-integration.single-instance. The engine process; a second one for the same
// data root exits 0. state/spawn.lock belongs to the clients, and the engine never takes it.
class EngineHost {
public:
    EngineHost(EngineCommandLine command_line, ports::PlatformServices platform);
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
