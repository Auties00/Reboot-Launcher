#include "reboot/os_windows/ipc/pipe_trust.hpp"

#include "win32.hpp"

#include <aclapi.h>
#include <sddl.h>

#include <cstdlib>
#include <utility>

#include "messages.hpp"
#include "token_info.hpp"
#include "unique_handle.hpp"
#include "wide.hpp"
#include "win32_errors.hpp"

namespace rb::os_windows::ipc {
namespace {

[[nodiscard]] Diagnostic client_unidentified(ULONG pid) {
    return make_diag(ErrorDomain::Platform, kPipeClientUnidentified).arg("pid", pid);
}

// A thread left running as the client would act with the client's identity.
void revert_or_die() noexcept {
    if (!RevertToSelf()) std::abort();
}

struct ClientToken {
    UniqueHandle token;
    // The client allowed only anonymous access.
    bool anonymous = false;
};

[[nodiscard]] Result<ClientToken> open_client_token(HANDLE pipe) {
    if (!ImpersonateNamedPipeClient(pipe)) {
        const DWORD error = GetLastError();
        if (error == ERROR_CANNOT_IMPERSONATE) return ClientToken{{}, true};
        return std::unexpected(call_failed("ImpersonateNamedPipeClient", error));
    }
    HANDLE token = nullptr;
    const BOOL opened = OpenThreadToken(GetCurrentThread(), TOKEN_QUERY, TRUE, &token);
    const DWORD error = GetLastError();
    revert_or_die();
    if (opened) return ClientToken{UniqueHandle{token}, false};
    if (error == ERROR_CANT_OPEN_ANONYMOUS || error == ERROR_BAD_IMPERSONATION_LEVEL) return ClientToken{{}, true};
    return std::unexpected(call_failed("OpenThreadToken", error));
}

[[nodiscard]] Result<SECURITY_IMPERSONATION_LEVEL> impersonation_level(HANDLE token) {
    SECURITY_IMPERSONATION_LEVEL level = SecurityAnonymous;
    DWORD size = 0;
    if (!GetTokenInformation(token, TokenImpersonationLevel, &level, sizeof(level), &size))
        return std::unexpected(call_failed("GetTokenInformation"));
    return level;
}

[[nodiscard]] Result<std::string> pipe_owner(HANDLE pipe) {
    PSID owner = nullptr;
    PSECURITY_DESCRIPTOR descriptor = nullptr;
    const DWORD error =
        GetSecurityInfo(pipe, SE_KERNEL_OBJECT, OWNER_SECURITY_INFORMATION, &owner, nullptr, nullptr, nullptr, &descriptor);
    if (error != ERROR_SUCCESS) return std::unexpected(call_failed("GetSecurityInfo", error));
    Result<std::string> sid = sid_string(owner);
    LocalFree(descriptor);
    return sid;
}

// The user of process `pid`; nullopt when its token is closed to us, as an elevated server's is.
[[nodiscard]] Result<std::optional<std::string>> process_user(ULONG pid) {
    const UniqueHandle process{OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid)};
    if (!process) {
        const DWORD error = GetLastError();
        if (error == ERROR_ACCESS_DENIED) return std::nullopt;
        return std::unexpected(call_failed("OpenProcess", error));
    }
    HANDLE raw = nullptr;
    if (!OpenProcessToken(process.get(), TOKEN_QUERY, &raw)) {
        const DWORD error = GetLastError();
        if (error == ERROR_ACCESS_DENIED) return std::nullopt;
        return std::unexpected(call_failed("OpenProcessToken", error));
    }
    const UniqueHandle token{raw};
    Result<std::string> sid = token_user_sid(token.get());
    if (!sid) return std::unexpected(std::move(sid.error()));
    return std::optional<std::string>{std::move(*sid)};
}

}  // namespace

Result<PipeTrust> PipeTrust::for_current_process() {
    HANDLE raw = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &raw)) return std::unexpected(call_failed("OpenProcessToken"));
    const UniqueHandle token{raw};
    Result<std::string> sid = token_user_sid(token.get());
    if (!sid) return std::unexpected(std::move(sid.error()));
    return PipeTrust{std::move(*sid)};
}

ports::PeerIdentity PipeTrust::self() const { return ports::PeerIdentity{user_sid_, GetCurrentProcessId()}; }

Result<std::vector<u8>> PipeTrust::pipe_security_descriptor() const {
    // FW includes FILE_CREATE_PIPE_INSTANCE, which every instance after the first needs.
    const std::wstring sid = to_wide(user_sid_);
    const std::wstring sddl = L"O:" + sid + L"D:P(A;;FRFW;;;" + sid + L")(A;;FRFW;;;SY)S:(ML;;NW;;;ME)";
    PSECURITY_DESCRIPTOR descriptor = nullptr;
    ULONG size = 0;
    if (!ConvertStringSecurityDescriptorToSecurityDescriptorW(sddl.c_str(), SDDL_REVISION_1, &descriptor, &size))
        return std::unexpected(call_failed("ConvertStringSecurityDescriptorToSecurityDescriptorW"));
    const auto* bytes = static_cast<const u8*>(descriptor);
    std::vector<u8> result(bytes, bytes + size);
    LocalFree(descriptor);
    return result;
}

Result<ports::PeerIdentity> PipeTrust::verify_client(PipeHandle pipe) const {
    ULONG pid = 0;
    if (!GetNamedPipeClientProcessId(pipe, &pid)) return std::unexpected(call_failed("GetNamedPipeClientProcessId"));
    Result<ClientToken> client = open_client_token(pipe);
    if (!client) return std::unexpected(std::move(client.error()));
    if (client->anonymous) return std::unexpected(untrusted(client_unidentified(pid)));
    Result<SECURITY_IMPERSONATION_LEVEL> level = impersonation_level(client->token.get());
    if (!level) return std::unexpected(std::move(level.error()));
    if (*level < SecurityIdentification) return std::unexpected(untrusted(client_unidentified(pid)));
    Result<std::string> sid = token_user_sid(client->token.get());
    if (!sid) return std::unexpected(std::move(sid.error()));
    if (*sid != user_sid_)
        return std::unexpected(untrusted(make_diag(ErrorDomain::Platform, kPipeClientOtherUser)
                                             .arg("pid", pid)
                                             .arg("client_sid", *sid)
                                             .arg("expected_sid", user_sid_)));
    return ports::PeerIdentity{std::move(*sid), static_cast<u32>(pid)};
}

Result<VerifiedServer> PipeTrust::verify_server(PipeHandle pipe) const {
    Result<std::string> owner = pipe_owner(pipe);
    if (!owner) return std::unexpected(std::move(owner.error()));
    if (*owner != user_sid_)
        return std::unexpected(untrusted(make_diag(ErrorDomain::Platform, kPipeOwnerMismatch)
                                             .arg("owner_sid", *owner)
                                             .arg("expected_sid", user_sid_)));
    ULONG pid = 0;
    if (!GetNamedPipeServerProcessId(pipe, &pid)) return std::unexpected(call_failed("GetNamedPipeServerProcessId"));
    Result<std::optional<std::string>> server = process_user(pid);
    if (!server) return std::unexpected(std::move(server.error()));
    VerifiedServer verified{ports::PeerIdentity{user_sid_, static_cast<u32>(pid)}, std::nullopt};
    if (!*server) {
        verified.warning =
            make_diag(ErrorDomain::Platform, kPipeServerUnverifiable).arg("pid", pid).severity(Severity::Warning).build();
    } else if (**server != user_sid_) {
        return std::unexpected(untrusted(make_diag(ErrorDomain::Platform, kPipeServerOtherUser)
                                             .arg("pid", pid)
                                             .arg("server_sid", **server)
                                             .arg("expected_sid", user_sid_)));
    }
    return verified;
}

}  // namespace rb::os_windows::ipc
