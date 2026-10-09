#include "token_info.hpp"

#include <sddl.h>

#include <vector>

#include "wide.hpp"
#include "win32_errors.hpp"

namespace reboot::os_windows::ipc {

Result<std::string> sid_string(PSID sid) {
    wchar_t* text = nullptr;
    if (!ConvertSidToStringSidW(sid, &text)) return std::unexpected(call_failed("ConvertSidToStringSidW"));
    std::string result = to_utf8(text);
    LocalFree(text);
    return result;
}

Result<std::string> token_user_sid(HANDLE token) {
    DWORD size = 0;
    GetTokenInformation(token, TokenUser, nullptr, 0, &size);
    if (size == 0) return std::unexpected(call_failed("GetTokenInformation"));
    // TOKEN_USER holds a pointer, so the buffer must be pointer-aligned.
    std::vector<void*> buffer((size + sizeof(void*) - 1) / sizeof(void*));
    if (!GetTokenInformation(token, TokenUser, buffer.data(), size, &size))
        return std::unexpected(call_failed("GetTokenInformation"));
    return sid_string(reinterpret_cast<const TOKEN_USER*>(buffer.data())->User.Sid);
}

}  // namespace reboot::os_windows::ipc
