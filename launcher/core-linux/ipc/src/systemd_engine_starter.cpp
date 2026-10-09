#include "reboot/os_linux/ipc/systemd_engine_starter.hpp"

#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <sys/stat.h>
#include <vector>

#include "detached_spawn.hpp"
#include "engine_environment.hpp"
#include "engine_socket_path.hpp"
#include "engine_start_plan.hpp"
#include "engine_units.hpp"
#include "reboot/foundation/log.hpp"
#include "state_locks.hpp"
#include "systemd_unit_state.hpp"
#include "tool_run.hpp"

extern char** environ;

namespace rb::os_linux::ipc {
namespace {

// The output of a systemctl or systemd-run call that ran and exited 0.
[[nodiscard]] std::optional<std::string> run_systemd(std::span<const std::string> argv) {
    const std::optional<ToolRun> run = run_tool(argv, SystemdEngineStarter::kSystemdCallDeadline);
    if (run && run->exit_code == 0) return run->output;
    REBOOT_LOG_DEBUG(Client, "{} {} did not succeed (exit {})", argv[0], argv.size() > 1 ? argv.back() : "",
                     run ? std::to_string(run->exit_code) : std::string("none"));
    return std::nullopt;
}

[[nodiscard]] bool socket_present(const NativePath& path) {
    struct stat info {};
    return ::lstat(path.c_str(), &info) == 0 && S_ISSOCK(info.st_mode);
}

[[nodiscard]] std::vector<std::string_view> inherited_environment() {
    std::vector<std::string_view> environment;
    for (char** entry = environ; entry != nullptr && *entry != nullptr; ++entry) environment.emplace_back(*entry);
    return environment;
}

[[nodiscard]] EngineEnvironmentInputs environment_inputs(const LinuxClientPaths& paths) {
    return {paths.home(),      paths.user_name(),  paths.login_shell(),
            paths.data_home(), paths.cache_home(), paths.state_home()};
}

// Step 1: the installed socket unit, which spawns the engine on the client's connect.
[[nodiscard]] bool start_socket_unit(const NativePath& socket_path) {
    const std::optional<std::string> shown = run_systemd(show_unit_argv(kEngineSocketUnit));
    if (!shown) return false;
    const SocketUnitAction action =
        socket_unit_action(SystemdUnitState::parse(*shown), socket_path, socket_present(socket_path));
    if (action == SocketUnitAction::Skip) return false;
    return run_systemd(socket_unit_argv(action)).has_value();
}

// Step 2: a transient unit of the user manager; nullopt moves on to step 3.
[[nodiscard]] std::optional<ports::StartResult> start_transient_unit(std::string_view root_hash,
                                                                     std::span<const std::string> pinned,
                                                                     std::span<const std::string> command) {
    const std::string unit = transient_unit_name(root_hash);
    const std::optional<std::string> shown = run_systemd(show_unit_argv(unit + ".service"));
    if (!shown) return std::nullopt;
    if (SystemdUnitState::parse(*shown).running) return ports::StartResult::AlreadyRunning;
    if (!run_systemd(systemd_run_argv(unit, pinned, command))) return std::nullopt;
    return ports::StartResult::Started;
}

}  // namespace

Result<ports::StartResult> SystemdEngineStarter::ensure_started(const NativePath& engine_exe, const DataRoot& root) {
    if (caller_.elevated) return ports::StartResult::ElevatedRefused;

    const AppLayout layout{root, paths_};
    if (auto created = create_private_dirs(layout.spawn_lock().parent_path()); !created)
        return std::unexpected(std::move(created.error()));
    const Result<posix::UniqueFd> spawn_lock = lock_ofd_exclusive(layout.spawn_lock());
    if (!spawn_lock) return std::unexpected(spawn_lock.error());
    const Result<bool> engine_running = is_ofd_locked(layout.engine_lock());
    if (!engine_running) return std::unexpected(engine_running.error());
    if (*engine_running) return ports::StartResult::AlreadyRunning;

    const std::string root_hash = root_hash16(canonical_root(root));
    const IpcRuntimeBase& runtime_base = paths_.runtime_base();
    if (!root.overridden && runtime_base.from_xdg_runtime_dir &&
        start_socket_unit(engine_socket_path(runtime_base.path, root_hash)))
        return ports::StartResult::Started;

    const EngineEnvironmentInputs inputs = environment_inputs(paths_);
    const std::vector<std::string> command = engine_command(engine_exe, paths_.appimage());
    if (runtime_base.from_xdg_runtime_dir) {
        const std::vector<std::string> pinned = pinned_engine_variables(inputs, root);
        if (const std::optional<ports::StartResult> started = start_transient_unit(root_hash, pinned, command))
            return *started;
    }

    if (under_steam_reaper_) return ports::StartResult::ConnectOnly;
    if (!caller_.interactive) return ports::StartResult::NoInteractiveSession;
    const std::vector<std::string_view> inherited = inherited_environment();
    const NativePath program{command.front()};
    if (auto spawned = spawn_detached({
            .program = program,
            .argv = command,
            .envp = engine_environment(inherited, inputs, root),
            .cwd = program.parent_path(),
        });
        !spawned)
        return std::unexpected(std::move(spawned.error()));
    return ports::StartResult::Started;
}

}  // namespace rb::os_linux::ipc
