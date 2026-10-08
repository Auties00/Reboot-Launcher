#pragma once

#include <memory>
#include <optional>
#include <string>
#include <thread>

#include "client_context.hpp"
#include "reboot/foundation/clock.hpp"
#include "reboot/foundation/diag.hpp"
#include "reboot/foundation/executor.hpp"
#include "reboot/ports/file_system.hpp"
#include "reboot/ports/ipc.hpp"
#include "reboot/ports/platform_services.hpp"

namespace reboot::client {

// Covers no capability ids. The OS ports, clock and executor thread one rb_ctx runs on.
class ClientRuntime {
public:
    // ports::make_client_platform() and REBOOT_LAUNCHER_HOME; starts the executor thread.
    [[nodiscard]] static Result<std::unique_ptr<ClientRuntime>> create();

    ClientRuntime(const ClientRuntime&) = delete;
    ClientRuntime& operator=(const ClientRuntime&) = delete;
    ~ClientRuntime();

    [[nodiscard]] ClientDeps deps();
    // Joins the executor thread; tasks still queued never run.
    void stop();

private:
    // Reads REBOOT_LAUNCHER_HOME once.
    ClientRuntime(ports::ClientPlatform platform, std::unique_ptr<ports::IFileSystem> files, ports::PeerIdentity self);

    ports::ClientPlatform platform_;
    std::unique_ptr<ports::IFileSystem> files_;
    const ports::PeerIdentity self_;
    const std::optional<std::string> launcher_home_;
    SystemClock clock_;
    Strand executor_;
    std::thread executor_thread_;
};

}  // namespace reboot::client

// rb_ctx_destroy closes the context and stops the runtime first, so no task outlives the context.
struct rb_ctx {
    std::unique_ptr<reboot::client::ClientRuntime> runtime;
    std::unique_ptr<reboot::client::ClientContext> context;
};
