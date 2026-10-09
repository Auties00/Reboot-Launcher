#pragma once

#include <optional>
#include <span>
#include <string_view>

#include "reboot/compat/runner_profile.hpp"
#include "reboot/foundation/diag.hpp"
#include "reboot/foundation/native_path.hpp"
#include "reboot/foundation/types.hpp"

namespace rb::ports {
class IFileSystem;
}

namespace rb::compat {

// Blocking helpers of PrefixManager, run on the WorkerPool. Failures are compat.prefix_failed
// naming `step`, unless noted.

enum class PrefixState : u8 { Missing, Unusable, Usable };

inline constexpr std::string_view kStepInspect = "inspect";
inline constexpr std::string_view kStepKillServer = "kill_server";
inline constexpr std::string_view kStepSetAside = "set_aside";
inline constexpr std::string_view kStepBoot = "boot";
inline constexpr std::string_view kStepCarrySaved = "carry_saved";
inline constexpr std::string_view kStepSeedRhi = "seed_rhi";

[[nodiscard]] Diagnostic prefix_failed(RunnerKind kind, std::string_view step);

// Usable once wineboot left drive_c and system.reg.
[[nodiscard]] PrefixState inspect_prefix(const NativePath& prefix);

// Whether a game DLL, or a DLL beside it that one imports, needs the VC++ runtime. A game DLL that
// cannot be read or parsed fails with its own error; a neighbour that cannot is skipped.
[[nodiscard]] Result<bool> game_needs_vc_runtime(ports::IFileSystem& fs, std::span<const NativePath> game_dlls);

// <prefixes_dir>/<runner_name>.backup-<version>, with path separators in `version` replaced.
[[nodiscard]] NativePath backup_path(const NativePath& prefixes_dir, RunnerKind kind, std::string_view version);
// Copies `prefix`, links as links, to backup_path(), then removes every earlier backup of the
// runner; compat.prefix_backup_failed, and no partial copy is left.
[[nodiscard]] Result<void> back_up_prefix(ports::IFileSystem& fs, const NativePath& prefixes_dir, RunnerKind kind,
                                          const NativePath& prefix, std::string_view version);

// <prefixes_dir>/<runner_name>.old, where an unusable prefix waits until its replacement exists.
[[nodiscard]] NativePath aside_path(const NativePath& prefixes_dir, RunnerKind kind);
// Moves `prefix` to `aside`. An existing aside is replaced only when `prefix` holds FortniteGame/Saved
// itself; otherwise `prefix` is removed and the aside kept.
[[nodiscard]] Result<void> set_aside(ports::IFileSystem& fs, RunnerKind kind, const NativePath& prefix,
                                     const NativePath& aside);
// Moves FortniteGame/Saved from the old tree's AppData/Local to the new prefix's, unless the new
// one already has it, then removes the old tree.
[[nodiscard]] Result<void> carry_saved(ports::IFileSystem& fs, RunnerKind kind, const NativePath& old_tree,
                                       const NativePath& prefix);

// Whether a user of `tree` has FortniteGame/Saved.
[[nodiscard]] bool holds_saved(const NativePath& tree);
// drive_c/users/<user>, the one profile that is not Public.
[[nodiscard]] std::optional<NativePath> prefix_user_dir(const NativePath& prefix);
// PreferredRHI=dx11 in the prefix user's GameUserSettings.ini; nothing when it already holds.
[[nodiscard]] Result<void> seed_rhi(ports::IFileSystem& fs, RunnerKind kind, const NativePath& prefix);

}  // namespace rb::compat
