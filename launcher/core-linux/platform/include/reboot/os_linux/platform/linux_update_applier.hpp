#pragma once

#include <optional>
#include <string>
#include <vector>

#include "reboot/foundation/diag.hpp"
#include "reboot/foundation/native_path.hpp"
#include "reboot/ports/os_services.hpp"
#include "reboot/posix/unique_fd.hpp"

namespace reboot::os_linux::platform {

class XdgPaths;

// Covers no capability ids; IUpdateApplier for the self-installed tarball and the AppImage.
// The engine execve()s the stable entry in its own PID, so systemd never restarts the unit.
// updates owns state/update-in-progress: the tarball shim counts starts there, so the engine
// runs with UpdateOptions::shim_counts_attempts for Tarball only. An AppImage has no shim and
// no rollback; it gives up like Windows and macOS.
class LinuxUpdateApplier final : public ports::IUpdateApplier {
public:
    // `paths` outlives the applier. When LISTEN_PID is this process, an O_CLOEXEC dup of
    // systemd's socket (fd 3) is kept, since the IPC listener closes its own copy on drain.
    LinuxUpdateApplier(const XdgPaths& paths, bool in_container);

    // `package` was verified by updates. Tarball: a .tar.zst holding one directory named after
    // the version, extracted into versions/.staging-<random>, then renamed to versions/<v>.
    // Entries are refused (platform.update_entry_unsafe) when absolute, holding "..", a link
    // resolving outside the staging directory, or a device, FIFO or socket node; setuid, setgid
    // and sticky bits are dropped. versions/<v> that neither link targets are removed first.
    // AppImage: copied to .<name>.update beside $APPIMAGE and made 0755. Nothing live changes.
    Result<void> stage(const NativePath& package) override;
    // Tarball: `previous` takes the current target, `current` the staged version, then the
    // version's bin/reboot-engine replaces the shim, each by renaming a fresh file over the old.
    // AppImage: the update is renamed over $APPIMAGE and run with "engine" before `args`.
    // Both dup2 the kept socket to fd 3, which clears FD_CLOEXEC, leave LISTEN_* as they are
    // and execve with the engine's own environment. On failure every link is restored.
    Result<void> apply_and_restart(std::vector<std::string> args) override;
    // False in a container and for Dev installs, so updates runs UpdateMode::NotifyOnly.
    [[nodiscard]] bool supports_in_place() const override;

private:
    const XdgPaths& paths_;
    bool in_container_ = false;
    std::optional<posix::UniqueFd> inherited_socket_;
};

}  // namespace reboot::os_linux::platform
