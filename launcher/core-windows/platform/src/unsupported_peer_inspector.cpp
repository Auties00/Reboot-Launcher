#include "reboot/os_windows/platform/unsupported_peer_inspector.hpp"

#include "messages.hpp"

namespace reboot::os_windows::platform {

Result<std::optional<u32>> UnsupportedPeerInspector::peer_uid(Endpoint, Endpoint) {
    return make_diag(ErrorDomain::Platform, kNotSupported).kind(ErrorKind::Unsupported).fail();
}

}  // namespace reboot::os_windows::platform
