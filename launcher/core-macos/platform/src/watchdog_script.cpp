#include "watchdog_script.hpp"

namespace reboot::os_macos::platform {

std::string watchdog_script(std::optional<u32> pid) {
    // `read` returns on EOF, which comes only when the engine closes its end or dies.
    return "read _; kill -KILL " + (pid ? std::to_string(*pid) : std::string{"0"});
}

}  // namespace reboot::os_macos::platform
