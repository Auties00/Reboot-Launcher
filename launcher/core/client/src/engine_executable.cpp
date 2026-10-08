#include "engine_executable.hpp"

namespace reboot::client {

NativePath engine_executable(const ports::IPlatformPaths& paths) {
#if defined(_WIN32)
    return paths.exe_dir() / "reboot-engine.exe";
#else
    return paths.exe_dir() / "reboot-engine";
#endif
}

}  // namespace reboot::client
