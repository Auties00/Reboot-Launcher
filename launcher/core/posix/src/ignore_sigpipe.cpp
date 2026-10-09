#include "reboot/posix/ignore_sigpipe.hpp"

#include <cerrno>
#include <signal.h>

#include "reboot/posix/posix_error.hpp"

namespace rb::posix {

Result<void> ignore_sigpipe() {
    struct sigaction action {};
    action.sa_handler = SIG_IGN;
    sigemptyset(&action.sa_mask);
    if (::sigaction(SIGPIPE, &action, nullptr) != 0) return std::unexpected(call_failed("sigaction", errno));
    return {};
}

}  // namespace rb::posix
