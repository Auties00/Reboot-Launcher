#include <cstdio>
#include <string_view>
#include <utility>
#include <vector>

#include "reboot/engine/engine_command_line.hpp"
#include "reboot/engine/engine_host.hpp"
#include "reboot/foundation/diag.hpp"
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
    Result<ports::PlatformServices> platform = ports::make_platform(options);
    if (!platform) return fail(platform.error());

    engine::EngineHost host(*command_line, std::move(*platform));
    return host.run();
}
