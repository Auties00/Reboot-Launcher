#include "win32.hpp"  // first: sets _WIN32_WINNT before any std header pulls in <windows.h>

#include "reboot/os_windows/platform/win32_session_host.hpp"

#include <array>
#include <chrono>
#include <mutex>
#include <utility>
#include <vector>

#include "messages.hpp"
#include "process_token.hpp"
#include "reboot/foundation/operation.hpp"
#include "reboot/foundation/sha256.hpp"
#include "reboot/ports/file_system.hpp"
#include "session_mapping.hpp"
#include "wide.hpp"

namespace rb::os_windows::platform {

namespace {

namespace wh = contracts::winhost;
using namespace std::chrono_literals;

// One DLL load, and the killed Job emptying; both scale with the runner.
constexpr std::chrono::milliseconds kInjectTimeout = 10s;
constexpr std::chrono::milliseconds kDrainTimeout = 5s;

// on_event is reached from win32session's watcher threads and from stop(), one at a time.
class EventRelay {
public:
    explicit EventRelay(UniqueFunction<void(ports::SessionHostEvent)> on_event) : on_event_(std::move(on_event)) {}

    void emit(ports::SessionHostEvent event) {
        std::scoped_lock lock(mutex_);
        if (on_event_) on_event_(std::move(event));
    }

private:
    std::mutex mutex_;
    UniqueFunction<void(ports::SessionHostEvent)> on_event_;
};

[[nodiscard]] wh::InjectSpec spec_of(const ports::InjectEntry& entry) {
    return wh::InjectSpec{utf16_bytes(shell_path(entry.path)), entry.sha256, entry.strategy, entry.phase};
}

// Holds `entry` deny-write, then checks its hash through that handle, so the bytes checked are
// the bytes loaded.
[[nodiscard]] Result<ports::HeldFile> hold_verified(ports::IFileSystem& fs, const ports::InjectEntry& entry) {
    auto held = fs.open_deny_write(entry.path);
    if (!held) {
        if (held.error().kind != ErrorKind::NotFound) return std::unexpected(std::move(held.error()));
        return make_diag(ErrorDomain::Platform, kFileVanished)
            .arg("path", entry.path)
            .kind(ErrorKind::NotFound)
            .cause(std::move(held.error()))
            .fail();
    }
    Sha256 hasher;
    std::vector<u8> chunk(64 * 1024);
    for (;;) {
        auto got = held->read(chunk);
        if (!got) return std::unexpected(std::move(got.error()));
        if (*got == 0) break;
        hasher.update(std::span<const u8>(chunk.data(), *got));
    }
    const std::array<u8, 32> digest = hasher.finish();
    if (!constant_time_equal(digest, entry.sha256))
        return make_diag(ErrorDomain::Platform, kPayloadHashMismatch).arg("path", entry.path).fail();
    return std::move(*held);
}

class Win32GameSession final : public ports::IGameSession {
public:
    Win32GameSession(ports::IFileSystem& fs, std::shared_ptr<EventRelay> relay,
                     std::vector<std::pair<NativePath, ports::HeldFile>> held)
        : fs_(fs), relay_(std::move(relay)), held_(std::move(held)) {}

    void attach(std::unique_ptr<win32session::Win32Session> session) { session_ = std::move(session); }

    Result<void> inject(const ports::InjectEntry& entry) override {
        if (auto held = hold(entry); !held) return held;
        auto injected = session_->inject(spec_of(entry));
        if (!injected) return std::unexpected(inject_diagnostic(injected.error(), entry.path));
        return {};
    }

    Result<void> resume() override {
        auto resumed = session_->resume();
        if (!resumed) return std::unexpected(spawn_diagnostic(resumed.error(), NativePath()));
        return {};
    }

    void stop(std::chrono::milliseconds grace) override {
        auto stopped = session_->stop(grace);
        if (!stopped)
            relay_->emit(ports::HostFatal{make_diag(ErrorDomain::Platform, kSessionStuck)
                                              .arg("count", static_cast<u64>(stopped.error().pids.size()))
                                              .build()});
    }

private:
    [[nodiscard]] Result<void> hold(const ports::InjectEntry& entry) {
        for (const auto& [path, file] : held_)
            if (path == entry.path) return {};
        auto file = hold_verified(fs_, entry);
        if (!file) return std::unexpected(std::move(file.error()));
        held_.emplace_back(entry.path, std::move(*file));
        return {};
    }

    ports::IFileSystem& fs_;
    std::shared_ptr<EventRelay> relay_;
    // Declared before the session, so the tree is gone before the DLLs are released.
    std::vector<std::pair<NativePath, ports::HeldFile>> held_;
    std::unique_ptr<win32session::Win32Session> session_;
};

}  // namespace

Result<std::unique_ptr<ports::IGameSession>> Win32SessionHost::launch(const ports::SessionLaunch& launch,
                                                                    UniqueFunction<void(ports::SessionHostEvent)> on_event) {
    std::vector<std::pair<NativePath, ports::HeldFile>> held;
    for (const ports::InjectEntry& entry : launch.inject) {
        auto file = hold_verified(fs_, entry);
        if (!file) return std::unexpected(std::move(file.error()));
        held.emplace_back(entry.path, std::move(*file));
    }

    auto base = user_environment();
    if (!base) return std::unexpected(std::move(base.error()));

    wh::SpawnGame spawn;
    spawn.exe_utf16 = utf16_bytes(shell_path(launch.exe));
    for (const std::string& arg : launch.args) spawn.argv_utf16.push_back(utf16_bytes(widen(arg)));
    spawn.env_block_utf16 = utf16_bytes(build_environment_block(*base, launch.env.vars));
    spawn.cwd_utf16 = utf16_bytes(shell_path(launch.cwd.empty() ? launch.exe.parent_path() : launch.cwd));
    for (const ports::CompanionSpec& companion : launch.companions) {
        wh::CompanionSpawn spawned;
        spawned.exe_utf16 = utf16_bytes(shell_path(companion.exe));
        for (const std::string& arg : companion.args) spawned.argv_utf16.push_back(utf16_bytes(widen(arg)));
        spawn.companions.push_back(std::move(spawned));
    }
    for (const ports::InjectEntry& entry : launch.inject) spawn.inject.push_back(spec_of(entry));
    for (const NativePath& parked : launch.park) spawn.park_utf16.push_back(utf16_bytes(shell_path(parked)));
    spawn.inject_timeout_ms = static_cast<u32>(scaled(kInjectTimeout, launch.multiplier).count());
    spawn.drain_timeout_ms = static_cast<u32>(scaled(kDrainTimeout, launch.multiplier).count());

    auto relay = std::make_shared<EventRelay>(std::move(on_event));
    auto session = std::make_unique<Win32GameSession>(fs_, relay, std::move(held));
    auto launched = win32session::launch_session(
        spawn, [relay](win32session::SessionEvent event) { relay->emit(to_port_event(event)); });
    if (!launched) return std::unexpected(spawn_diagnostic(launched.error(), launch.exe));
    session->attach(std::move(*launched));
    return std::unique_ptr<ports::IGameSession>(std::move(session));
}

}  // namespace rb::os_windows::platform
