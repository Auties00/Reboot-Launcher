#pragma once

#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "reboot/foundation/cancel.hpp"
#include "reboot/foundation/diag.hpp"
#include "reboot/foundation/native_path.hpp"
#include "reboot/foundation/types.hpp"
#include "reboot/foundation/user_request.hpp"
#include "reboot/ports/process.hpp"
#include "reboot/ports/runner.hpp"

namespace rb::posix {
class TokenFiles;
}

namespace rb::os_macos::runner {

enum class HostCpu : u8 { AppleSilicon, Intel };

// Covers no capability ids (decisions macos-compat-layer, owner-2, process-model).
// Play runs only on Apple Silicon; hosting never reaches this platform.
class DxmtWineRunnerPlatform final : public ports::IRunnerPlatform {
public:
    // winhost's token files go under `token_base` (see token_base()).
    DxmtWineRunnerPlatform(HostCpu cpu, NativePath token_base);
    ~DxmtWineRunnerPlatform() override;
    DxmtWineRunnerPlatform(const DxmtWineRunnerPlatform&) = delete;
    DxmtWineRunnerPlatform& operator=(const DxmtWineRunnerPlatform&) = delete;

    // <per-user temp dir>/reboot, the temp dir being confstr's _CS_DARWIN_USER_TEMP_DIR (0700
    // already); <data_root>/winhost-tokens when confstr names none.
    [[nodiscard]] static NativePath token_base(const NativePath& data_root);

    [[nodiscard]] std::vector<ports::RunnerKind> supported() const override;

    // Blocking (MacRuntimeLayout::resolve), so callers run it on a worker.
    Result<ports::RuntimeLayout> layout(ports::RunnerKind kind, const ports::RuntimeDirs& dirs) override;

    // Blocking tree walk, so callers run it on a worker. Symlinks are not followed.
    Result<void> post_extract(const NativePath& runtime_dir) override;

    // `base` already holds the runner layer from EnvBuilder; this adds only WINEPREFIX.
    Result<ports::ProcessLaunch> runner_launch(const ports::RuntimeLayout& layout, const NativePath& prefix,
                                               const NativePath& winhost_exe, ports::EnvBlock base) override;

    // Boot runs `wine wineboot -u`, KillServer the runtime's `wineserver -k`, Run `wine <exe> <args>`;
    // each adds only WINEPREFIX to `base`.
    Result<ports::ProcessLaunch> prefix_command(const ports::RuntimeLayout& layout, const NativePath& prefix,
                                                const ports::PrefixCommand& command, ports::EnvBlock base) override;

    // Nothing to set up on macOS.
    Result<std::optional<std::string>> runtime_setup(const ports::RuntimeLayout& layout, CancelToken token) override;

    // Blocking stat of the Rosetta daemon, so callers run it on a worker.
    [[nodiscard]] std::optional<UserRequestKind> pending_prerequisite() override;

    // posix::TokenFiles under the token base.
    Result<std::unique_ptr<ports::ITokenFile>> write_token_file(std::string_view token) override;
    void sweep_token_files() override;

private:
    HostCpu cpu_;
    std::unique_ptr<posix::TokenFiles> token_files_;
};

}  // namespace rb::os_macos::runner
