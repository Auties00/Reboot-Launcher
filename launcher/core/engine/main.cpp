#include <cstdio>
#include <cstdlib>
#include <memory>
#include <string_view>
#include <utility>
#include <vector>

#include "reboot/engine/engine_command_line.hpp"
#include "reboot/engine/engine_host.hpp"
#include "reboot/foundation/diag.hpp"
#include "reboot/ports/ipc.hpp"
#include "reboot/ports/platform_services.hpp"

namespace {

// Nothing is logged yet, so a failure before EngineHost::run prints only its message id.
int fail(const reboot::Diagnostic& diag) {
    std::fprintf(stderr, "reboot-engine: %s\n", diag.id.c_str());
    return reboot::exit_code_for(diag);
}

}  // namespace

int main(int argc, char** argv) {
    using namespace reboot;

    const std::vector<std::string_view> args(argv + 1, argv + argc);
    Result<engine::EngineCommandLine> command_line = engine::parse_command_line(args);
    if (!command_line) return fail(command_line.error());

    ports::PlatformOptions options;
    options.foreground = command_line->origin == engine::EngineOrigin::Foreground;
#ifndef _WIN32
    // The platform must use EngineHost's data root; on Windows it reads the wide variable itself.
    if (const char* home = std::getenv("REBOOT_LAUNCHER_HOME"); home != nullptr && *home != '\0')
        options.data_root_override = NativePath(home);
#endif
    Result<ports::PlatformServices> platform = ports::make_platform(options);
    if (!platform) return fail(platform.error());
    Result<ports::EngineEndpoint> endpoint = ports::make_engine_endpoint();
    if (!endpoint) return fail(endpoint.error());
    platform->ipc_listener = std::move(endpoint->listener);

    // The client side of our own endpoint, for an update's self-test; without one the test fails.
    std::unique_ptr<ports::IIpcConnector> self_test;
    if (Result<ports::ClientPlatform> client = ports::make_client_platform()) self_test = std::move(client->connector);

    engine::EngineHost host(*command_line, std::move(*platform), std::move(endpoint->self), std::move(self_test));
    return host.run();
}
