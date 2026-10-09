#include "win32.hpp"  // first: sets _WIN32_WINNT before any std header pulls in <windows.h>

#include "process_token.hpp"

#include <cstring>

#include "unique_handle.hpp"
#include "wide.hpp"
#include "win_error.hpp"

namespace rb::os_windows::platform {

namespace {

[[nodiscard]] Result<UniqueHandle> open_own_token() {
    HANDLE token = nullptr;
    if (OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token) == 0)
        return std::unexpected(call_failed("OpenProcessToken", GetLastError()));
    return UniqueHandle(token);
}

}  // namespace

Result<UserSid> UserSid::current() {
    auto token = open_own_token();
    if (!token) return std::unexpected(std::move(token.error()));
    DWORD size = 0;
    GetTokenInformation(token->get(), TokenUser, nullptr, 0, &size);
    std::vector<u8> buffer(size);
    if (size == 0 || GetTokenInformation(token->get(), TokenUser, buffer.data(), size, &size) == 0)
        return std::unexpected(call_failed("GetTokenInformation", GetLastError()));
    const auto* user = reinterpret_cast<const TOKEN_USER*>(buffer.data());
    const DWORD length = GetLengthSid(user->User.Sid);
    std::vector<u8> sid(length);
    if (CopySid(length, sid.data(), user->User.Sid) == 0) return std::unexpected(call_failed("CopySid", GetLastError()));
    return UserSid(std::move(sid));
}

Result<std::string> UserSid::to_string() const {
    wchar_t* text = nullptr;
    if (ConvertSidToStringSidW(get(), &text) == 0) return std::unexpected(call_failed("ConvertSidToStringSidW", GetLastError()));
    std::string result = narrow(text);
    LocalFree(text);
    return result;
}

Result<std::vector<std::wstring>> user_environment() {
    auto token = open_own_token();
    if (!token) return std::unexpected(std::move(token.error()));
    void* block = nullptr;
    if (CreateEnvironmentBlock(&block, token->get(), FALSE) == 0)
        return std::unexpected(call_failed("CreateEnvironmentBlock", GetLastError()));
    std::vector<std::wstring> entries;
    for (const wchar_t* entry = static_cast<const wchar_t*>(block); *entry != L'\0'; entry += std::wcslen(entry) + 1)
        entries.emplace_back(entry);
    DestroyEnvironmentBlock(block);
    return entries;
}

bool process_elevated() noexcept {
    HANDLE token = nullptr;
    if (OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token) == 0) return false;
    TOKEN_ELEVATION elevation{};
    DWORD size = 0;
    const bool got = GetTokenInformation(token, TokenElevation, &elevation, sizeof elevation, &size) != 0;
    CloseHandle(token);
    return got && elevation.TokenIsElevated != 0;
}

Result<OwnerOnlyDacl> OwnerOnlyDacl::create(bool container) {
    auto sid = UserSid::current();
    if (!sid) return std::unexpected(std::move(sid.error()));
    const DWORD size = static_cast<DWORD>(sizeof(ACL) + sizeof(ACCESS_ALLOWED_ACE) + GetLengthSid(sid->get()));
    std::vector<u8> acl(size);
    auto* list = reinterpret_cast<PACL>(acl.data());
    if (InitializeAcl(list, size, ACL_REVISION) == 0) return std::unexpected(call_failed("InitializeAcl", GetLastError()));
    const DWORD inherit = container ? static_cast<DWORD>(OBJECT_INHERIT_ACE | CONTAINER_INHERIT_ACE) : 0;
    if (AddAccessAllowedAceEx(list, ACL_REVISION, inherit, FILE_ALL_ACCESS, sid->get()) == 0)
        return std::unexpected(call_failed("AddAccessAllowedAceEx", GetLastError()));
    return OwnerOnlyDacl(std::move(*sid), std::move(acl));
}

SECURITY_ATTRIBUTES* OwnerOnlyDacl::attributes() noexcept {
    InitializeSecurityDescriptor(&descriptor_, SECURITY_DESCRIPTOR_REVISION);
    SetSecurityDescriptorDacl(&descriptor_, TRUE, acl(), FALSE);
    // Protected, so the parent's inheritable ACEs never widen it.
    SetSecurityDescriptorControl(&descriptor_, SE_DACL_PROTECTED, SE_DACL_PROTECTED);
    attributes_.nLength = sizeof attributes_;
    attributes_.lpSecurityDescriptor = &descriptor_;
    attributes_.bInheritHandle = FALSE;
    return &attributes_;
}

}  // namespace rb::os_windows::platform
