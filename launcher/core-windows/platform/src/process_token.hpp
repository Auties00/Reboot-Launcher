#pragma once

#include <string>
#include <vector>

#include "reboot/foundation/diag.hpp"
#include "reboot/foundation/types.hpp"
#include "win32.hpp"

namespace rb::os_windows::platform {

// The engine process token's user SID, as a self-relative copy.
class UserSid {
public:
    [[nodiscard]] static Result<UserSid> current();

    [[nodiscard]] PSID get() const noexcept { return const_cast<u8*>(bytes_.data()); }
    // "S-1-5-21-...".
    [[nodiscard]] Result<std::string> to_string() const;

private:
    explicit UserSid(std::vector<u8> bytes) : bytes_(std::move(bytes)) {}

    std::vector<u8> bytes_;
};

// "NAME=value" entries of CreateEnvironmentBlock for the engine's token, without the caller's
// own environment: what a fresh logon of this user starts with.
[[nodiscard]] Result<std::vector<std::wstring>> user_environment();

[[nodiscard]] bool process_elevated() noexcept;

// An owner-only DACL for the engine's user: full control, protected from inheritance, and passed
// on to children when `container` is set.
class OwnerOnlyDacl {
public:
    [[nodiscard]] static Result<OwnerOnlyDacl> create(bool container);

    [[nodiscard]] PACL acl() const noexcept { return reinterpret_cast<PACL>(const_cast<u8*>(acl_.data())); }
    // For CreateDirectoryW and CreateFileW; valid while this object lives and is not moved.
    [[nodiscard]] SECURITY_ATTRIBUTES* attributes() noexcept;

private:
    OwnerOnlyDacl(UserSid sid, std::vector<u8> acl) : sid_(std::move(sid)), acl_(std::move(acl)) {}

    UserSid sid_;
    std::vector<u8> acl_;
    SECURITY_DESCRIPTOR descriptor_{};
    SECURITY_ATTRIBUTES attributes_{};
};

}  // namespace rb::os_windows::platform
