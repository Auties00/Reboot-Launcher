#pragma once

#include <chrono>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "reboot/foundation/clock.hpp"
#include "reboot/foundation/diag.hpp"
#include "reboot/foundation/executor.hpp"
#include "reboot/foundation/function.hpp"
#include "reboot/foundation/types.hpp"
#include "reboot/ports/process.hpp"
#include "reboot/testing/fake_os.hpp"
#include "reboot/testing/scripted_child.hpp"
#include "reboot/testing/stdio_peer.hpp"

namespace reboot::testing {

// Decides what a matching spawn becomes. An error from `on_spawn` is what spawn() returns.
struct SpawnRule {
    UniqueFunction<bool(const ports::ProcessLaunch&)> matches;
    UniqueFunction<Result<void>(ScriptedChild&)> on_spawn;
    // Retired after its first match.
    bool once = false;
};

// What a Job kill leaves: the Windows adapters call TerminateJobObject(job, 1).
inline constexpr int kJobKillExitCode = 1;

// Covers no capability ids (decision testing-strategy).
// IProcessLauncher whose children are scripted by rules tried in order; an unmatched spawn fails
// with testing.unscripted_spawn. Pids count up from 1000 and `created` comes from the clock.
class ScriptedProcessLauncher final : public ports::IProcessLauncher {
public:
    // terminate_tree exits a child as `os` kills it: kJobKillExitCode on Windows, signal 9 elsewhere.
    ScriptedProcessLauncher(Executor& io, const IClock& clock, FakeOs os);
    ~ScriptedProcessLauncher() override;
    ScriptedProcessLauncher(const ScriptedProcessLauncher&) = delete;
    ScriptedProcessLauncher& operator=(const ScriptedProcessLauncher&) = delete;

    Result<std::unique_ptr<ports::ChildProcess>> spawn(const ports::ProcessLaunch& launch) override;
    Result<bool> is_alive(u32 pid, std::chrono::system_clock::time_point created) override;
    Result<void> kill(u32 pid, std::chrono::system_clock::time_point created) override;

    void add_rule(SpawnRule rule);
    // Shorthands for spawns whose exe file name equals `file_name`.
    void on_exe(std::string_view file_name, UniqueFunction<Result<void>(ScriptedChild&)> on_spawn);
    // Runs a fresh peer for each spawn, e.g. a FakeBackend behind reboot-backend.
    void serve_exe(std::string_view file_name,
                   UniqueFunction<std::unique_ptr<IStdioPeer>(const ports::ProcessLaunch&)> make_peer);
    void fail_exe(std::string_view file_name, Diagnostic error);

    // A process left by an earlier engine run, for orphan reaping from runtime.json; kill() ends it.
    void add_orphan(u32 pid, std::chrono::system_clock::time_point created);

    // Every child spawned so far, oldest first.
    [[nodiscard]] std::vector<ScriptedChild*> children() const;
    [[nodiscard]] ScriptedChild* last(std::string_view file_name) const;
    [[nodiscard]] std::vector<u32> killed() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace reboot::testing
