#pragma once

#include <memory>
#include <optional>
#include <string>
#include <thread>

#include "client_context.hpp"
#include "connect_settings.hpp"
#include "reboot/client.h"
#include "reboot/foundation/clock.hpp"
#include "reboot/foundation/diag.hpp"
#include "reboot/foundation/executor.hpp"
#include "reboot/ports/platform_services.hpp"

namespace rb::client {

// Covers no capability ids. The OS ports, clock and executor threads one rb_ctx runs on.
class ClientRuntime {
public:
    // ports::make_client_platform() and REBOOT_LAUNCHER_HOME; starts the executor threads.
    [[nodiscard]] static Result<std::unique_ptr<ClientRuntime>> create();
    // internal.bug when a port is missing.
    [[nodiscard]] static Result<std::unique_ptr<ClientRuntime>> create(ports::ClientPlatform platform);

    ClientRuntime(const ClientRuntime&) = delete;
    ClientRuntime& operator=(const ClientRuntime&) = delete;
    ~ClientRuntime();

    [[nodiscard]] ClientDeps deps();
    // Joins the executor threads; tasks still queued never run.
    void stop();

private:
    // Reads REBOOT_LAUNCHER_HOME once.
    explicit ClientRuntime(ports::ClientPlatform platform);

    ports::ClientPlatform platform_;
    const std::optional<std::string> launcher_home_;
    SystemClock clock_;
    Strand executor_;
    // Apart from executor_, so a blocked connect round delays no deadline or wake.
    Strand link_executor_;
    std::thread executor_thread_;
    std::thread link_thread_;
};

}  // namespace rb::client

// rb_ctx_destroy closes the context and stops the runtime first, so no task outlives the context.
struct rb_ctx {
    std::unique_ptr<rb::client::ClientRuntime> runtime;
    std::unique_ptr<rb::client::ClientContext> context;
};

namespace rb::client {

// rb_ctx_create once the options are read: builds the context on `runtime` and waits for connect.
// The status and rb_last_error are rb_ctx_create's.
[[nodiscard]] rb_status open_context(std::unique_ptr<ClientRuntime> runtime, ConnectSettings settings, rb_ctx** out);

}  // namespace rb::client
