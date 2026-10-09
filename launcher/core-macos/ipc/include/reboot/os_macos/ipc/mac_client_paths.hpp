#pragma once

#include <optional>

#include "reboot/foundation/diag.hpp"
#include "reboot/foundation/native_path.hpp"
#include "reboot/ports/platform_paths.hpp"

namespace rb::os_macos::ipc {

// Covers no capability ids; IPlatformPaths for reboot_client, which may not link
// core-macos/platform. Every method follows the same rule as os_macos::platform::MacPaths.
class MacClientPaths final : public ports::IPlatformPaths {
public:
    // Resolves everything once: the home directory from getpwuid_r (not $HOME), the per-user
    // temp directory from confstr(_CS_DARWIN_USER_TEMP_DIR), and the image holding this code
    // (dladdr) through realpath. A translocated bundle keeps its translocated path;
    // SecTranslocateIsTranslocatedURL is not in the public SDK headers and is looked up with dlsym.
    [[nodiscard]] static Result<MacClientPaths> detect();

    // ~/Library/Application Support/Reboot Launcher.
    [[nodiscard]] NativePath default_data_root() const override;
    // ~/Library/Caches/Reboot Launcher.
    [[nodiscard]] NativePath default_cache_root() const override;
    // ~/Library/Logs/Reboot Launcher.
    [[nodiscard]] NativePath default_logs_root() const override;
    // The per-user temp directory itself; the endpoint adds reboot-launcher/<hash16>.sock.
    [[nodiscard]] NativePath ipc_runtime_base() const override;
    // <X>.app/Contents/MacOS, where reboot-engine ships, when the library sits in Contents/MacOS
    // or Contents/Frameworks of a bundle; the library's own directory otherwise.
    [[nodiscard]] NativePath exe_dir() const override;
    // AppBundle inside a .app, Dev otherwise.
    [[nodiscard]] ports::InstallKind install_kind() const override;
    // The .app bundle, which Velopack replaces as a whole; nullopt outside a bundle or when
    // translocated, since a swap of the read-only translocation mount cannot succeed.
    [[nodiscard]] std::optional<NativePath> velopack_package_dir() const override;

private:
    MacClientPaths(NativePath home, NativePath user_temp_dir, NativePath exe_dir, std::optional<NativePath> app_bundle,
                   bool translocated);

    NativePath home_;
    NativePath user_temp_dir_;
    NativePath exe_dir_;
    std::optional<NativePath> app_bundle_;
    bool translocated_ = false;
};

}  // namespace rb::os_macos::ipc
