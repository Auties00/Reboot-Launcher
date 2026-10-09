#include "reboot/os_macos/ipc/mac_caller_context.hpp"

#include "unistd.hpp"

#include <Security/AuthSession.h>

#include "caller_facts.hpp"
#include "messages.hpp"

namespace reboot::os_macos::ipc {

Result<MacCallerContext> MacCallerContext::detect() {
    SecuritySessionId session_id = 0;
    SessionAttributeBits attributes = 0;
    const OSStatus status = ::SessionGetInfo(callerSecuritySession, &session_id, &attributes);
    if (status != errSessionSuccess) {
        return make_diag(ErrorDomain::Platform, kCallerSessionUnreadable)
            .os(SystemError{SystemError::Origin::Host, status})
            .fail();
    }
    return MacCallerContext{caller_context_from({
        .audit_session_id = static_cast<u32>(session_id),
        .graphic_access = (attributes & sessionHasGraphicAccess) != 0,
        .euid = static_cast<u32>(::geteuid()),
    })};
}

void MacCallerContext::allow_foreground(u32 /*pid*/) {}

}  // namespace reboot::os_macos::ipc
