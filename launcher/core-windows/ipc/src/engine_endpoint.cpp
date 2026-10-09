#include <memory>
#include <utility>

#include "reboot/os_windows/ipc/named_pipe_listener.hpp"
#include "reboot/os_windows/ipc/pipe_trust.hpp"
#include "reboot/ports/platform_services.hpp"

namespace reboot::ports {

Result<EngineEndpoint> make_engine_endpoint() {
    using namespace os_windows::ipc;
    Result<PipeTrust> trust = PipeTrust::for_current_process();
    if (!trust) return std::unexpected(std::move(trust.error()));
    EngineEndpoint endpoint;
    endpoint.self = trust->self();
    endpoint.listener = std::make_unique<NamedPipeListener>(std::move(*trust));
    return endpoint;
}

}  // namespace reboot::ports
