#include "wait_status.hpp"

namespace reboot::os_macos::platform {

ports::ChildExit decode_wait_status(int status) noexcept {
    const int low = status & 0x7F;
    ports::ChildExit exit;
    if (low == 0) {
        exit.code = (status >> 8) & 0xFF;
    } else if (low != 0x7F) {
        exit.signal = low;
    }
    return exit;
}

}  // namespace reboot::os_macos::platform
