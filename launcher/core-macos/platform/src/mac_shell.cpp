#include "darwin.hpp"

#include "reboot/os_macos/platform/mac_shell.hpp"

#include "apple_shims.hpp"
#include "https_url.hpp"
#include "messages.hpp"

namespace rb::os_macos::platform {

namespace {

[[nodiscard]] Result<void> no_gui_session() {
    return make_diag(ErrorDomain::Platform, kNoGuiSession).kind(ErrorKind::Unsupported).fail();
}

}  // namespace

Result<void> MacShell::open_url(std::string_view https_url) {
    if (!is_https_url(https_url)) return make_diag(ErrorDomain::Platform, kUrlNotHttps).kind(ErrorKind::InvalidInput).fail();
    if (!aqua_session_) return no_gui_session();
    return shims::workspace_open(https_url);
}

Result<void> MacShell::open_path(const NativePath& path) {
    if (!aqua_session_) return no_gui_session();
    return shims::workspace_open_file(path);
}

Result<void> MacShell::reveal(const NativePath& path) {
    if (!aqua_session_) return no_gui_session();
    return shims::workspace_reveal(path);
}

Result<void> MacShell::trash(const NativePath& path) { return shims::file_manager_trash(path); }

}  // namespace rb::os_macos::platform
