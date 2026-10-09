#pragma once

#include <optional>
#include <string>

#include "reboot/foundation/diag.hpp"
#include "reboot/foundation/native_path.hpp"
#include "reboot/foundation/types.hpp"

namespace rb::integration {

enum class ShellErrorCode : u8 {
    // A window would open in a session the caller does not see.
    EngineInOtherSession,
    // The caller sent neither DISPLAY nor WAYLAND_DISPLAY.
    NoDisplay,
    NotHttps,
    NotAbsolute,
    ShellFailed,
    Cancelled,
};

enum class ShellAction : u8 { OpenUrl, OpenPath, Reveal };

struct ShellError {
    ShellErrorCode code{};
    ShellAction action{};
    // The link for OpenUrl.
    std::string url;
    std::optional<NativePath> path;
    std::string caller_session;
    std::string engine_session;
    std::optional<Diagnostic> cause;
};

[[nodiscard]] Diagnostic to_diagnostic(const ShellError& error);

}  // namespace rb::integration
