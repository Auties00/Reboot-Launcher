#include "watchdog_script.hpp"

namespace rb::os_macos::platform {

std::string watchdog_script(std::optional<u32> pid) {
    // Signals the child sends its own group are ignored, so only EOF from the engine ends `read`.
    return "trap '' HUP INT QUIT TERM; read _; kill -KILL " + (pid ? std::to_string(*pid) : std::string{"0"});
}

}  // namespace rb::os_macos::platform
