#include "reboot/play/display_context.hpp"

#include <algorithm>

#include "messages.hpp"

namespace reboot::play {

namespace {

constexpr std::string_view kDisplay = "DISPLAY";
constexpr std::string_view kWaylandDisplay = "WAYLAND_DISPLAY";

}  // namespace

Result<void> check_display(const DisplayContext& caller, std::string_view engine_os_session, SessionMatch match) {
    if (match == SessionMatch::OsSession) {
        if (caller.os_session == engine_os_session) return {};
        return make_diag(ErrorDomain::Play, msg::kWrongSession).kind(ErrorKind::Conflict).fail();
    }
    const bool usable = std::ranges::any_of(caller.display_env, [](const contracts::ipc::EnvVar& var) {
        return (var.name == kDisplay || var.name == kWaylandDisplay) && !var.value.empty();
    });
    if (usable) return {};
    return make_diag(ErrorDomain::Play, msg::kNoDisplay).kind(ErrorKind::Unsupported).fail();
}

}  // namespace reboot::play
