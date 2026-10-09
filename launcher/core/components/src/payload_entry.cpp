#include "reboot/components/payload_entry.hpp"

#include "messages.hpp"

namespace rb::components {

Result<void> check_payload_abi(const PayloadEntry& entry) {
    if (entry.payload_abi == VersionStreams::payload_abi) return {};
    return make_diag(ErrorDomain::Components, kPayloadAbiMismatch)
        .arg("version", entry.version)
        .arg("payload_abi", entry.payload_abi)
        .arg("expected", VersionStreams::payload_abi)
        .kind(ErrorKind::Unsupported)
        .fail();
}

}  // namespace rb::components
