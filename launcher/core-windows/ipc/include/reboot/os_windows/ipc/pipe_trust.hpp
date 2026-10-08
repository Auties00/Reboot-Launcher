#pragma once

#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "reboot/foundation/diag.hpp"
#include "reboot/foundation/types.hpp"
#include "reboot/ports/ipc.hpp"

namespace reboot::os_windows::ipc {

// A Win32 HANDLE; public headers include no Windows SDK header.
using PipeHandle = void*;

// The engine as the client verified it.
struct VerifiedServer {
    ports::PeerIdentity peer;
    // platform.pipe_server_unverifiable at Warning when only the pipe owner could be checked.
    std::optional<Diagnostic> warning;
};

// Covers no capability ids; the same-user rule both ends of the engine pipe enforce.
// Failing Win32 calls are platform.ipc_call_failed with the SystemError.
class PipeTrust {
public:
    // TokenUser of the process token, as an "S-1-5-..." string.
    [[nodiscard]] static Result<PipeTrust> for_current_process();

    [[nodiscard]] const std::string& user_sid() const noexcept { return user_sid_; }
    [[nodiscard]] ports::PeerIdentity self() const;

    // Owner is the user SID even when elevated, so a non-elevated client's owner check passes.
    // DACL: the user and LocalSystem only. SACL: Medium label, NO_WRITE_UP.
    [[nodiscard]] Result<std::vector<u8>> pipe_security_descriptor() const;

    // Engine side, before any byte is read. TokenUser, TokenSessionId and the impersonation level
    // come from one ImpersonateNamedPipeClient token; the session fills the peer's os_session.
    // Below SecurityIdentification: platform.pipe_client_unidentified; another user:
    // platform.pipe_client_other_user; both are causes of ipc.endpoint_untrusted.
    [[nodiscard]] Result<ports::PeerIdentity> verify_client(PipeHandle pipe) const;

    // Client side, before the first write. The owner SID must be ours (platform.pipe_owner_mismatch)
    // and so must the server process's user (platform.pipe_server_other_user); both are causes of
    // ipc.endpoint_untrusted. An unreadable server token (an elevated engine) passes on the owner
    // alone: only SeRestorePrivilege could set another user's pipe owner to our SID.
    [[nodiscard]] Result<VerifiedServer> verify_server(PipeHandle pipe) const;

private:
    explicit PipeTrust(std::string user_sid) : user_sid_(std::move(user_sid)) {}

    std::string user_sid_;
};

}  // namespace reboot::os_windows::ipc
