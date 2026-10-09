#include "reboot/os_windows/platform/windows_prerequisites.hpp"

#include <string>

#include "messages.hpp"
#include "os_version.hpp"

namespace rb::os_windows::platform {

std::vector<ports::PrerequisiteStatus> WindowsPrerequisites::check() {
    const OsVersion version = read_os_version();
    const bool met = version.major > 10 || (version.major == 10 && version.build >= kMinimumBuild);
    ports::PrerequisiteStatus status;
    status.id = std::string(kMinimumBuildId);
    status.met = met;
    if (!met) status.remediation_message_id = kWindowsTooOld;
    return {status};
}

Result<void> WindowsPrerequisites::remediate(std::string_view id) {
    const ErrorKind kind = id == kMinimumBuildId ? ErrorKind::Unsupported : ErrorKind::InvalidInput;
    return make_diag(ErrorDomain::Platform, kNoRemediation).arg("id", id).kind(kind).fail();
}

}  // namespace rb::os_windows::platform
