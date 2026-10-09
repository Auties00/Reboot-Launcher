#include "reboot/os_windows/ipc/windows_engine_starter.hpp"

#include <chrono>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "engine_environment_block.hpp"
#include "engine_launch.hpp"
#include "engine_task.hpp"
#include "messages.hpp"
#include "spawn_lock.hpp"
#include "unique_handle.hpp"
#include "wide.hpp"
#include "win32.hpp"
#include "win32_errors.hpp"

namespace rb::os_windows::ipc {
namespace {

constexpr DWORD kReadyPoll = 25;

// Without connecting, so a listening engine sees no client.
[[nodiscard]] bool pipe_exists(const std::wstring& name) {
    if (WaitNamedPipeW(name.c_str(), 1)) return true;
    return GetLastError() == ERROR_SEM_TIMEOUT;
}

// Holds spawn.lock until the engine's pipe exists, so the next holder finds it; gives up early
// when `process` exits, as an engine that lost the engine.lock race does.
void wait_for_pipe(const std::wstring& name, HANDLE process, std::chrono::milliseconds wait) {
    const auto give_up = std::chrono::steady_clock::now() + wait;
    while (!pipe_exists(name) && std::chrono::steady_clock::now() < give_up) {
        if (process == nullptr)
            Sleep(kReadyPoll);
        else if (WaitForSingleObject(process, kReadyPoll) == WAIT_OBJECT_0)
            return;
    }
}

// Step 1. nullopt when breakaway is refused (ERROR_ACCESS_DENIED), which leaves step 2.
[[nodiscard]] Result<std::optional<UniqueHandle>> spawn_detached(const NativePath& engine_exe, const DataRoot& root) {
    std::vector<std::wstring> inherited;
    if (wchar_t* strings = GetEnvironmentStringsW(); strings != nullptr) {
        inherited = environment_entries(strings);
        FreeEnvironmentStringsW(strings);
    }
    std::wstring environment = engine_environment_block(inherited, root);
    std::wstring command_line = engine_command_line(engine_exe);
    const NativePath directory = engine_exe.parent_path();
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION process{};
    if (!CreateProcessW(engine_exe.c_str(), command_line.data(), nullptr, nullptr, FALSE,
                        DETACHED_PROCESS | CREATE_NEW_PROCESS_GROUP | CREATE_BREAKAWAY_FROM_JOB | CREATE_UNICODE_ENVIRONMENT,
                        environment.data(), directory.c_str(), &startup, &process)) {
        const DWORD error = GetLastError();
        if (error == ERROR_ACCESS_DENIED) return std::nullopt;
        return std::unexpected(make_diag(ErrorDomain::Platform, kEngineSpawnFailed)
                                   .arg("path", engine_exe)
                                   .os(SystemError{SystemError::Origin::Host, static_cast<i64>(error)})
                                   .build());
    }
    CloseHandle(process.hThread);
    return std::optional<UniqueHandle>{UniqueHandle{process.hProcess}};
}

}  // namespace

Result<ports::StartResult> WindowsEngineStarter::ensure_started(const NativePath& engine_exe, const DataRoot& root) {
    if (caller_.elevated) return ports::StartResult::ElevatedRefused;
    if (!caller_.interactive) return ports::StartResult::NoInteractiveSession;

    Result<SpawnLock> lock = SpawnLock::acquire(root.root / "state" / "spawn.lock", kSpawnLockWait);
    if (!lock) return std::unexpected(std::move(lock.error()));
    const std::wstring pipe =
        to_wide(ports::endpoint_name(ports::PeerIdentity{user_sid_, 0}, root_hash16(canonical_root(root))));
    if (pipe_exists(pipe)) return ports::StartResult::AlreadyRunning;

    Result<std::optional<UniqueHandle>> spawned = spawn_detached(engine_exe, root);
    if (!spawned) return std::unexpected(std::move(spawned.error()));
    if (*spawned) {
        wait_for_pipe(pipe, (*spawned)->get(), kEngineReadyWait);
        return ports::StartResult::Started;
    }

    // The registered task runs the engine on the default root only.
    const std::optional<u32> session = parse_session_id(caller_.os_session);
    if (!root.overridden && session) {
        Result<TaskRun> task = run_engine_task(engine_task_name(user_sid_), engine_exe, *session, kTaskSchedulerDeadline);
        if (!task) return std::unexpected(std::move(task.error()));
        if (*task == TaskRun::Running) return ports::StartResult::AlreadyRunning;
        if (*task == TaskRun::Started) {
            wait_for_pipe(pipe, nullptr, kEngineReadyWait);
            return ports::StartResult::Started;
        }
    }
    return ports::StartResult::CannotDetach;
}

}  // namespace rb::os_windows::ipc
