#pragma once

#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "reboot/foundation/cancel.hpp"
#include "reboot/foundation/diag.hpp"
#include "reboot/foundation/native_path.hpp"
#include "reboot/foundation/types.hpp"
#include "reboot/foundation/user_request.hpp"
#include "reboot/ports/process.hpp"

namespace reboot::ports {

enum class RunnerKind : u8 { Native, Umu, Wine, MacRuntime };

// `entry` is the wine binary, or umu-run for Umu.
struct RuntimeLayout {
    NativePath root;
    NativePath entry;
    std::vector<std::pair<std::string, std::string>> env;
    NativePath winhost_in_prefix_dir;
};

// The unpacked runtimes one runner's layout is resolved from.
struct RuntimeDirs {
    // GE-Proton for Umu, the Wine build otherwise.
    NativePath runtime;
    // Umu only: the umu-launcher.
    std::optional<NativePath> launcher;
};

// Boot creates or upgrades the prefix; KillServer ends its wineserver; Run starts a Windows program.
enum class PrefixVerb : u8 { Boot, KillServer, Run };

struct PrefixCommand {
    PrefixVerb verb{};
    // Run only.
    NativePath exe;
    std::vector<std::string> args;
};

// Play only: hosting never runs under Wine.
class IRunnerPlatform {
public:
    virtual ~IRunnerPlatform() = default;

    [[nodiscard]] virtual std::vector<RunnerKind> supported() const = 0;
    virtual Result<RuntimeLayout> layout(RunnerKind kind, const RuntimeDirs& dirs) = 0;
    // Removes the quarantine attribute on macOS; nothing elsewhere.
    virtual Result<void> post_extract(const NativePath& runtime_dir) = 0;
    virtual Result<ProcessLaunch> runner_launch(const RuntimeLayout& layout, const NativePath& prefix,
                                                const NativePath& winhost_exe, EnvBlock base) = 0;
    // `base` holds the runner layer, as for runner_launch; a Run exe's directory is exposed like winhost's.
    virtual Result<ProcessLaunch> prefix_command(const RuntimeLayout& layout, const NativePath& prefix,
                                                 const PrefixCommand& command, EnvBlock base) = 0;
    // The Steam Linux Runtime setup on Linux, returning the build it installed; nothing elsewhere.
    virtual Result<std::optional<std::string>> runtime_setup(const RuntimeLayout& layout, CancelToken token) = 0;
    // RosettaInstall when Rosetta is missing on Apple Silicon.
    [[nodiscard]] virtual std::optional<UserRequestKind> pending_prerequisite() = 0;
};

}  // namespace reboot::ports
