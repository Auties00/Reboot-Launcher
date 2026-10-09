#include "reboot/integration/shell_service.hpp"

#include <optional>
#include <string_view>
#include <utility>

#include "reboot/contracts/ipc.hpp"
#include "reboot/foundation/executor.hpp"
#include "reboot/integration/openable_url.hpp"
#include "reboot/integration/shell_error.hpp"
#include "reboot/ports/os_services.hpp"

namespace reboot::integration {

namespace {

[[nodiscard]] bool has_display(const contracts::ipc::CallerContext& caller) {
    for (const contracts::ipc::EnvVar& var : caller.display_env)
        if ((var.name == "DISPLAY" || var.name == "WAYLAND_DISPLAY") && !var.value.empty()) return true;
    return false;
}

}  // namespace

ShellService::ShellService(ports::IShellLauncher& shell, const ports::ISystemInfo& system, DesktopCheck desktop_check,
                           WorkerPool& workers, Executor& strand)
    : shell_(shell), system_(system), desktop_check_(desktop_check), workers_(workers), strand_(strand) {}

void ShellService::open_url(const contracts::ipc::CallerContext& caller, std::string url, CancelToken token,
                            Done done) {
    ShellError error{.code = ShellErrorCode::NotHttps, .action = ShellAction::OpenUrl, .url = url};
    if (!is_openable_url(url)) return fail(std::move(error), std::move(done));
    run(caller, std::move(error), std::move(token), std::move(done),
        [&shell = shell_, url = std::move(url)] { return shell.open_url(url); });
}

void ShellService::open_path(const contracts::ipc::CallerContext& caller, NativePath path, CancelToken token,
                             Done done) {
    ShellError error{.code = ShellErrorCode::NotAbsolute, .action = ShellAction::OpenPath, .path = path};
    if (!path.is_absolute()) return fail(std::move(error), std::move(done));
    run(caller, std::move(error), std::move(token), std::move(done),
        [&shell = shell_, path = std::move(path)] { return shell.open_path(path); });
}

void ShellService::reveal(const contracts::ipc::CallerContext& caller, NativePath path, CancelToken token,
                          Done done) {
    ShellError error{.code = ShellErrorCode::NotAbsolute, .action = ShellAction::Reveal, .path = path};
    if (!path.is_absolute()) return fail(std::move(error), std::move(done));
    run(caller, std::move(error), std::move(token), std::move(done),
        [&shell = shell_, path = std::move(path)] { return shell.reveal(path); });
}

std::optional<ShellError> ShellService::check_desktop(const contracts::ipc::CallerContext& caller,
                                                      const ShellError& base) const {
    ShellError error = base;
    switch (desktop_check_) {
        case DesktopCheck::OsSession: {
            std::string engine_session = system_.os_session();
            if (caller.os_session == engine_session) return std::nullopt;
            error.code = ShellErrorCode::EngineInOtherSession;
            error.caller_session = caller.os_session;
            error.engine_session = std::move(engine_session);
            return error;
        }
        case DesktopCheck::Display:
            if (has_display(caller)) return std::nullopt;
            error.code = ShellErrorCode::NoDisplay;
            return error;
    }
    return std::nullopt;
}

void ShellService::fail(ShellError error, Done done) {
    strand_.post([diag = to_diagnostic(error), done = std::move(done)]() mutable { done(std::unexpected(std::move(diag))); });
}

void ShellService::run(const contracts::ipc::CallerContext& caller, ShellError base, CancelToken token, Done done,
                       UniqueFunction<Result<void>()> action) {
    if (std::optional<ShellError> wrong_desktop = check_desktop(caller, base))
        return fail(std::move(*wrong_desktop), std::move(done));
    workers_.submit<void>(
        [action = std::move(action), base = std::move(base)](CancelToken cancel) mutable -> Result<void> {
            if (cancel.cancelled()) {
                base.code = ShellErrorCode::Cancelled;
                return std::unexpected(to_diagnostic(base));
            }
            if (Result<void> opened = action(); !opened) {
                base.code = ShellErrorCode::ShellFailed;
                base.cause = std::move(opened.error());
                return std::unexpected(to_diagnostic(base));
            }
            return {};
        },
        std::move(token), strand_, std::move(done));
}

}  // namespace reboot::integration
