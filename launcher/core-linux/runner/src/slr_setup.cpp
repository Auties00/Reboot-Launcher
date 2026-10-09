#include "reboot/os_linux/runner/slr_setup.hpp"

#include <condition_variable>
#include <filesystem>
#include <memory>
#include <mutex>
#include <optional>
#include <span>
#include <string>
#include <system_error>
#include <utility>

#include "env_vars.hpp"
#include "messages.hpp"
#include "reboot/foundation/log.hpp"
#include "reboot/posix/posix_error.hpp"

namespace reboot::os_linux::runner {

namespace {

namespace fs = std::filesystem;

// Proton's wine prefix inside umu's WINEPREFIX, and the wineserver that serves it.
constexpr std::string_view kProtonPrefix = "pfx";
constexpr std::string_view kProtonWineServer = "files/bin/wineserver";

// Whole lines of one output stream, for the Wine log; a line is split at kMaxLine bytes.
class OutputLines {
public:
    static constexpr std::size_t kMaxLine = 4096;

    void feed(std::span<const u8> bytes) {
        for (const u8 byte : bytes) {
            if (byte == '\n') {
                emit();
                continue;
            }
            partial_.push_back(static_cast<char>(byte));
            if (partial_.size() >= kMaxLine) emit();
        }
    }
    void finish() {
        if (!partial_.empty()) emit();
    }

private:
    void emit() {
        if (partial_.ends_with('\r')) partial_.pop_back();
        REBOOT_LOG_INFO(Wine, "{}", partial_);
        partial_.clear();
    }

    std::string partial_;
};

// Shared with the launcher's I/O-thread callbacks, which may outlive the wait.
struct Watch {
    std::mutex mutex;
    std::condition_variable changed;
    std::optional<ports::ChildExit> exit;
    bool cancelled = false;
    OutputLines out;
    OutputLines err;
};

struct Finished {
    ports::ChildExit exit;
    // Only when the cancel killed the child; an exit seen first stands.
    bool cancelled = false;
};

// Blocks until the child exits; cancelling `token` kills its tree first.
Result<Finished> run_to_exit(ports::IProcessLauncher& processes, const ports::ProcessLaunch& launch,
                             const CancelToken& token) {
    auto spawned = processes.spawn(launch);
    if (!spawned) return std::unexpected(std::move(spawned.error()));
    ports::ChildProcess& child = **spawned;

    const auto watch = std::make_shared<Watch>();
    if (launch.stdio == ports::StdioMode::Capture) {
        child.on_stdout([watch](std::span<const u8> bytes) {
            const std::lock_guard lock(watch->mutex);
            watch->out.feed(bytes);
        });
        child.on_stderr([watch](std::span<const u8> bytes) {
            const std::lock_guard lock(watch->mutex);
            watch->err.feed(bytes);
        });
    }
    child.on_exit([watch](ports::ChildExit exit) {
        const std::lock_guard lock(watch->mutex);
        watch->exit = exit;
        watch->changed.notify_all();
    });
    const CancelRegistration registration = token.on_cancel([watch](CancelReason) {
        const std::lock_guard lock(watch->mutex);
        watch->cancelled = true;
        watch->changed.notify_all();
    });

    std::unique_lock lock(watch->mutex);
    bool killed = false;
    while (!watch->exit) {
        if (watch->cancelled && !killed) {
            killed = true;
            lock.unlock();
            if (auto stopped = child.terminate_tree(); !stopped)
                REBOOT_LOG_WARN(Play, "The runtime setup could not be stopped: {}", stopped.error().id);
            lock.lock();
            continue;
        }
        watch->changed.wait(lock);
    }
    watch->out.finish();
    watch->err.finish();
    return Finished{*watch->exit, killed};
}

// Proton's wineserver lingers a few seconds after umu-run exits; the scratch prefix is ours alone.
void stop_wineserver(ports::IProcessLauncher& processes, const ports::EnvBlock& base, const NativePath& proton_root,
                     const NativePath& scratch, const NativePath& cwd) {
    ports::ProcessLaunch stop;
    stop.exe = proton_root / kProtonWineServer;
    stop.args = {"-k"};
    stop.env = base;
    // string() is the native bytes on POSIX.
    set_var(stop.env, "WINEPREFIX", (scratch / kProtonPrefix).string());
    stop.cwd = cwd;
    if (auto stopped = run_to_exit(processes, stop, CancelToken{}); !stopped)
        REBOOT_LOG_DEBUG(Play, "wineserver -k for the setup prefix did not run: {}", stopped.error().id);
}

Result<SlrBuild> setup_cancelled() {
    return make_diag(ErrorDomain::Platform, kSlrSetupCancelled).kind(ErrorKind::Cancelled).fail();
}

}  // namespace

SlrSetup::SlrSetup(ports::IProcessLauncher& processes, ports::EnvBlock base, NativePath folders)
    : processes_(processes), base_(std::move(base)), folders_(std::move(folders)) {}

Result<SlrBuild> SlrSetup::run(const ports::RuntimeLayout& layout, CancelToken token) {
    if (token.cancelled()) return setup_cancelled();
    const NativePath scratch = folders_ / kScratchPrefix;
    std::error_code error;
    fs::remove_all(scratch, error);
    if (!error) fs::create_directories(scratch, error);
    if (error)
        return make_diag(ErrorDomain::Platform, kScratchPrefixFailed)
            .arg("path", scratch)
            .os(posix::errno_error(error.value()))
            .fail();

    ports::ProcessLaunch setup;
    setup.exe = layout.entry;
    // An empty executable makes umu set up the runtime and the prefix, and run nothing.
    setup.args = {""};
    setup.env = base_;
    for (const auto& [name, value] : layout.env) set_var(setup.env, name, value);
    set_var(setup.env, "UMU_RUNTIME_UPDATE", "1");
    // string() is the native bytes on POSIX.
    set_var(setup.env, "WINEPREFIX", scratch.string());
    setup.cwd = folders_;
    setup.stdio = ports::StdioMode::Capture;
    setup.own_group = true;
    setup.scope_name = std::string(kScopeName);
    const auto finished = run_to_exit(processes_, setup, token);

    if (finished) stop_wineserver(processes_, base_, layout.root, scratch, folders_);
    fs::remove_all(scratch, error);
    if (error) REBOOT_LOG_WARN(Play, "{} could not be removed after the runtime setup", display_utf8(scratch));

    if (!finished) return make_diag(ErrorDomain::Platform, kSlrSetupNotStarted).cause(finished.error()).fail();
    if (finished->cancelled) return setup_cancelled();
    if (finished->exit.signal)
        return make_diag(ErrorDomain::Platform, kSlrSetupKilled).arg("signal", *finished->exit.signal).fail();
    if (finished->exit.code != 0)
        return make_diag(ErrorDomain::Platform, kSlrSetupFailed)
            .arg("exit_code", finished->exit.code.value_or(-1))
            .retryable()
            .fail();
    return SlrBuild::read(layout.root, folders_);
}

}  // namespace reboot::os_linux::runner
