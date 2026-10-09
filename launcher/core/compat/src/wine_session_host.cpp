#include "reboot/compat/wine_session_host.hpp"

#include <algorithm>
#include <chrono>
#include <concepts>
#include <cstddef>
#include <iterator>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

#include "messages.hpp"
#include "reboot/compat/runner_env.hpp"
#include "reboot/contracts/game_client.hpp"
#include "reboot/contracts/winhost.hpp"
#include "reboot/foundation/executor.hpp"
#include "reboot/foundation/log.hpp"
#include "reboot/foundation/secret.hpp"
#include "reboot/foundation/text.hpp"
#include "reboot/game_channel/game_channel_listener.hpp"
#include "reboot/game_channel/winhost_peer.hpp"
#include "reboot/ports/process.hpp"
#include "reboot/process/line_reader.hpp"
#include "reboot/process/wiping_launch.hpp"

namespace reboot::compat {

namespace {

namespace wh = contracts::winhost;
namespace gc = contracts::game_client;
using namespace std::chrono_literals;

// One DLL load, and the killed Job emptying; both scale with the runner.
constexpr std::chrono::milliseconds kInjectTimeout = 10s;
constexpr std::chrono::milliseconds kDrainTimeout = 5s;
constexpr std::string_view kWinhostRole = "winhost";
constexpr std::string_view kScopePrefix = "reboot-session-";
constexpr std::string_view kFilesystemsRw = "PRESSURE_VESSEL_FILESYSTEMS_RW";
// The shipping exe sits at <build>/FortniteGame/Binaries/Win64.
constexpr std::string_view kGameDir = "FortniteGame";

[[nodiscard]] std::string utf8_of(const NativePath& path) {
    const std::u8string text = path.u8string();
    return {text.begin(), text.end()};
}

void append_utf16(wh::Bytes& out, std::u16string_view text) {
    for (const char16_t unit : text) {
        out.push_back(static_cast<u8>(unit & 0xFF));
        out.push_back(static_cast<u8>(unit >> 8));
    }
}

[[nodiscard]] wh::Bytes utf16_bytes(std::u16string_view text) {
    wh::Bytes out;
    out.reserve(text.size() * 2);
    append_utf16(out, text);
    return out;
}

// For text that may carry a credential: the intermediate UTF-16 copy is wiped too.
[[nodiscard]] wh::Bytes secret_utf16_bytes(std::string_view utf8) {
    std::u16string wide = utf8_to_utf16(utf8);
    wh::Bytes out = utf16_bytes(wide);
    secure_wipe(wide.data(), wide.size() * sizeof(char16_t));
    return out;
}

void wipe(wh::Bytes& bytes) noexcept {
    secure_wipe(bytes.data(), bytes.size());
    bytes.clear();
}

// The argv and the environment block may carry credentials.
void wipe(wh::SpawnGame& spawn) noexcept {
    for (wh::Bytes& arg : spawn.argv_utf16) wipe(arg);
    wipe(spawn.env_block_utf16);
}

// "NAME=VALUE\0" per variable, then the final NUL. Reserved up front, since a reallocation would
// free an unwiped copy of the token.
[[nodiscard]] wh::Bytes environment_block(const ports::EnvBlock& env) {
    std::size_t units = 2;
    for (const auto& [name, value] : env.vars) units += name.size() + value.size() + 2;
    wh::Bytes block;
    block.reserve(units * 2);
    std::string entry;
    for (const auto& [name, value] : env.vars) {
        entry.assign(name).append(1, '=').append(value);
        std::u16string wide = utf8_to_utf16(entry);
        wide.push_back(u'\0');
        append_utf16(block, wide);
        secure_wipe(wide.data(), wide.size() * sizeof(char16_t));
        secure_wipe(entry.data(), entry.size());
    }
    if (block.empty()) block = {0, 0};
    block.push_back(0);
    block.push_back(0);
    return block;
}

[[nodiscard]] NativePath build_root(const NativePath& exe) {
    for (NativePath dir = exe.parent_path(); dir.has_relative_path(); dir = dir.parent_path())
        if (dir.filename() == kGameDir) return dir.parent_path();
    return exe.parent_path();
}

[[nodiscard]] ports::SessionRole role_of(wh::ProcessRole role) noexcept {
    return role == wh::ProcessRole::Game ? ports::SessionRole::Game : ports::SessionRole::Companion;
}

[[nodiscard]] std::optional<int> code_of(const std::optional<i64>& code) noexcept {
    if (!code) return std::nullopt;
    return static_cast<int>(*code);
}

[[nodiscard]] Diagnostic runner_diag(MessageId message, RunnerKind kind) {
    return make_diag(ErrorDomain::Compat, message).arg("runner", runner_name(kind)).build();
}

// What one launched session shares with its peer handlers and the runner's I/O callbacks.
struct SessionState {
    SessionState(Executor& strand_ref, TimerService& timers_ref, std::shared_ptr<WineLogLine> log)
        : strand(strand_ref), timers(timers_ref), wine_log(std::move(log)) {}

    ~SessionState() {
        if (spawn) wipe(*spawn);
    }

    Executor& strand;
    TimerService& timers;
    std::shared_ptr<WineLogLine> wine_log;
    SessionId session;
    RunnerKind kind{};
    PathMapper paths;
    UniqueFunction<void(ports::SessionHostEvent)> on_event;
    // Handed to winhost in its Welcome, then wiped.
    std::optional<wh::SpawnGame> spawn;
    std::unique_ptr<game_channel::WinhostPeer> peer;
    std::unique_ptr<ports::ChildProcess> runner;
    std::optional<process::LineReader> runner_out;
    std::optional<process::LineReader> runner_err;
    // The host path behind each Windows path given to winhost, for its Injected events.
    std::vector<std::pair<wh::Bytes, NativePath>> injected_paths;
    // Inject requests whose Injected event has not come yet.
    std::vector<wh::Bytes> awaiting_injected;
    // resume() and inject() calls made before the Welcome, in call order.
    std::vector<std::variant<std::monostate, wh::InjectSpec>> queued;
    TimerHandle stop_timer;
    std::optional<int> runner_exit;
    bool welcomed = false;
    bool game_exited = false;
    bool stopping = false;
    bool runner_exited = false;
    bool ended = false;
};

using StatePtr = std::shared_ptr<SessionState>;
using WeakState = std::weak_ptr<SessionState>;

void emit(const StatePtr& state, ports::SessionHostEvent event) {
    state->strand.post([weak = WeakState(state), event = std::move(event)]() mutable {
        const StatePtr locked = weak.lock();
        if (locked && locked->on_event) locked->on_event(std::move(event));
    });
}

[[nodiscard]] NativePath host_path_of(const SessionState& state, const wh::Bytes& windows) {
    for (const auto& [bytes, host] : state.injected_paths)
        if (bytes == windows) return host;
    std::u16string text(windows.size() / 2, u'\0');
    for (std::size_t i = 0; i < text.size(); ++i)
        text[i] = static_cast<char16_t>(windows[2 * i] | (windows[2 * i + 1] << 8));
    const std::string utf8 = utf16_to_utf8(text);
    return NativePath(std::u8string(utf8.begin(), utf8.end()));
}

// Drops the peer from a fresh strand task, since this may run inside one of its handlers.
void drop_peer(const StatePtr& state) {
    if (!state->peer) return;
    state->strand.post([peer = std::shared_ptr<game_channel::WinhostPeer>(std::move(state->peer))] {});
}

// Exited{Winhost} ends the session; HostFatal comes first unless the game exited or stop() asked.
void end(const StatePtr& state, Diagnostic fatal) {
    if (state->ended) return;
    state->ended = true;
    if (!state->game_exited && !state->stopping) emit(state, ports::HostFatal{std::move(fatal)});
    emit(state, ports::Exited{ports::SessionRole::Winhost, state->runner_exit});
    drop_peer(state);
}

void kill_tree(const StatePtr& state) {
    drop_peer(state);
    if (state->runner && !state->runner_exited) static_cast<void>(state->runner->terminate_tree());
    end(state, runner_diag(msg::kRunnerExited, state->kind));
}

void fail_inject(const StatePtr& state, const wh::Bytes& path, const Diagnostic& error) {
    const auto awaiting = std::ranges::find(state->awaiting_injected, path);
    if (awaiting == state->awaiting_injected.end()) return;
    state->awaiting_injected.erase(awaiting);
    std::optional<SystemError> os = error.os_error;
    for (const Diagnostic& cause : error.causes)
        if (!os && cause.os_error) os = cause.os_error;
    emit(state, ports::Injected{host_path_of(*state, path), false, os});
}

void send_resume(const StatePtr& state) {
    auto sent = state->peer->resume([weak = WeakState(state)](Result<void> reply) {
        const StatePtr locked = weak.lock();
        if (!locked || reply || locked->ended || locked->stopping) return;
        emit(locked, ports::HostFatal{std::move(reply.error())});
    });
    if (!sent && !state->ended) emit(state, ports::HostFatal{std::move(sent.error())});
}

void send_inject(const StatePtr& state, wh::InjectSpec spec) {
    wh::Bytes path = spec.path_utf16;
    state->awaiting_injected.push_back(path);
    auto sent = state->peer->inject(std::move(spec), [weak = WeakState(state), path](Result<void> reply) {
        const StatePtr locked = weak.lock();
        if (!locked) return;
        if (reply) {
            const auto awaiting = std::ranges::find(locked->awaiting_injected, path);
            if (awaiting != locked->awaiting_injected.end()) locked->awaiting_injected.erase(awaiting);
            return;
        }
        fail_inject(locked, path, reply.error());
    });
    if (!sent) fail_inject(state, path, sent.error());
}

void on_welcomed(const StatePtr& state) {
    state->welcomed = true;
    auto queued = std::move(state->queued);
    for (auto& request : queued) {
        if (!state->peer || state->ended) return;
        if (auto* spec = std::get_if<wh::InjectSpec>(&request)) send_inject(state, std::move(*spec));
        else send_resume(state);
    }
}

void on_winhost_event(const StatePtr& state, game_channel::WinhostEvent event) {
    std::visit(
        [&]<class E>(E& message) {
            if constexpr (std::same_as<E, wh::Spawned>) {
                emit(state, ports::Spawned{role_of(message.role), message.pid});
            } else if constexpr (std::same_as<E, wh::Injected>) {
                const auto awaiting = std::ranges::find(state->awaiting_injected, message.path_utf16);
                if (awaiting != state->awaiting_injected.end()) state->awaiting_injected.erase(awaiting);
                std::optional<SystemError> error;
                if (message.error) error = SystemError{SystemError::Origin::GuestWindows, *message.error};
                emit(state, ports::Injected{host_path_of(*state, message.path_utf16), message.ok, error});
            } else if constexpr (std::same_as<E, wh::Output>) {
                emit(state, ports::Output{role_of(message.role), message.stream, std::move(message.bytes)});
            } else if constexpr (std::same_as<E, wh::Exited>) {
                if (message.role == wh::ProcessRole::Game) state->game_exited = true;
                emit(state, ports::Exited{role_of(message.role), code_of(message.code)});
            } else {
                auto fatal = make_diag(ErrorDomain::Compat, msg::kWinhostFatal).arg("step", message.step);
                if (message.os_code) std::move(fatal).os({SystemError::Origin::GuestWindows, *message.os_code});
                emit(state, ports::HostFatal{std::move(fatal).build()});
            }
        },
        event);
}

void on_runner_exit(const StatePtr& state, ports::ChildExit exit) {
    state->runner_exited = true;
    state->runner_exit = exit.code;
    if (state->runner_out) state->runner_out->finish();
    if (state->runner_err) state->runner_err->finish();
    end(state, runner_diag(msg::kRunnerExited, state->kind));
}

class WineGameSession final : public ports::IGameSession {
public:
    explicit WineGameSession(StatePtr state) : state_(std::move(state)) {}

    ~WineGameSession() override {
        state_->on_event = nullptr;
        state_->stop_timer.cancel();
        state_->peer.reset();
        if (state_->runner && !state_->runner_exited) static_cast<void>(state_->runner->terminate_tree());
        state_->runner.reset();
    }

    WineGameSession(const WineGameSession&) = delete;
    WineGameSession& operator=(const WineGameSession&) = delete;

    Result<void> inject(const ports::InjectEntry& entry) override {
        if (state_->ended || !state_->peer) return std::unexpected(runner_diag(msg::kRunnerExited, state_->kind));
        auto windows = state_->paths.to_windows(entry.path);
        if (!windows) return std::unexpected(std::move(windows.error()));
        wh::InjectSpec spec{utf16_bytes(*windows), entry.sha256, entry.strategy, entry.phase};
        state_->injected_paths.emplace_back(spec.path_utf16, entry.path);
        if (!state_->welcomed) {
            state_->queued.emplace_back(std::move(spec));
            return {};
        }
        send_inject(state_, std::move(spec));
        return {};
    }

    Result<void> resume() override {
        if (state_->ended || !state_->peer) return std::unexpected(runner_diag(msg::kRunnerExited, state_->kind));
        if (!state_->welcomed) {
            state_->queued.emplace_back(std::monostate{});
            return {};
        }
        send_resume(state_);
        return {};
    }

    void stop(std::chrono::milliseconds grace) override {
        if (state_->ended || state_->stopping) return;
        state_->stopping = true;
        if (!state_->peer || !state_->welcomed) return kill_tree(state_);
        static_cast<void>(state_->peer->stop(grace, [](Result<void>) {}));
        state_->stop_timer = state_->timers.after(grace, [weak = WeakState(state_)] {
            if (const StatePtr locked = weak.lock()) kill_tree(locked);
        });
    }

private:
    StatePtr state_;
};

}  // namespace

struct WineSessionHost::Impl {
    explicit Impl(WineSessionHostDeps& deps)
        : processes(deps.processes),
          runner(deps.runner),
          channel(deps.channel),
          timers(deps.timers),
          strand(deps.strand),
          wine_log(std::make_shared<WineLogLine>(std::move(deps.wine_log))) {}

    // The runner layer, plus under Umu what pressure-vessel must share: the build, the injected
    // DLLs' directories and the Proton log directory; runner_launch adds winhost's.
    [[nodiscard]] Result<ports::EnvBlock> runner_vars(const WineSessionSetup& setup, const ports::SessionLaunch& launch) {
        ports::EnvBlock layer = runner_layer(setup.kind, setup.layout);
        if (setup.kind != RunnerKind::Umu) return layer;
        std::vector<NativePath> exposed{build_root(launch.exe)};
        for (const ports::InjectEntry& entry : launch.inject) exposed.push_back(entry.path.parent_path());
        if (!setup.log_dir.empty()) exposed.push_back(setup.log_dir);

        auto existing = std::ranges::find(layer.vars, kFilesystemsRw, &std::pair<std::string, std::string>::first);
        if (existing == layer.vars.end()) {
            layer.vars.emplace_back(std::string(kFilesystemsRw), std::string());
            existing = std::prev(layer.vars.end());
        }
        std::string& value = existing->second;
        for (const NativePath& path : exposed) {
            const std::string entry = utf8_of(path);
            if (entry.find(':') != std::string::npos)
                return make_diag(ErrorDomain::Compat, msg::kPathNotExposable).arg("path", path).kind(ErrorKind::InvalidInput).fail();
            const std::string_view current = value;
            bool present = false;
            for (std::size_t start = 0; start <= current.size() && !present;) {
                const std::size_t stop = std::min(current.find(':', start), current.size());
                present = current.substr(start, stop - start) == entry;
                start = stop + 1;
            }
            if (present) continue;
            if (!value.empty()) value += ':';
            value += entry;
        }
        return layer;
    }

    [[nodiscard]] Result<wh::SpawnGame> spawn_game(SessionState& state, const ports::SessionLaunch& launch) {
        wh::SpawnGame spawn;
        const auto map = [&](const NativePath& host) -> Result<wh::Bytes> {
            auto windows = state.paths.to_windows(host);
            if (!windows) return std::unexpected(std::move(windows.error()));
            return utf16_bytes(*windows);
        };
        const auto fail = [&](Diagnostic error) -> Result<wh::SpawnGame> {
            wipe(spawn);
            return std::unexpected(std::move(error));
        };

        auto exe = map(launch.exe);
        if (!exe) return fail(std::move(exe.error()));
        spawn.exe_utf16 = std::move(*exe);
        for (const std::string& arg : launch.args) spawn.argv_utf16.push_back(secret_utf16_bytes(arg));
        spawn.env_block_utf16 = environment_block(launch.env);
        auto cwd = map(launch.cwd.empty() ? launch.exe.parent_path() : launch.cwd);
        if (!cwd) return fail(std::move(cwd.error()));
        spawn.cwd_utf16 = std::move(*cwd);
        for (const ports::CompanionSpec& companion : launch.companions) {
            auto companion_exe = map(companion.exe);
            if (!companion_exe) return fail(std::move(companion_exe.error()));
            wh::CompanionSpawn spawned{std::move(*companion_exe), {}};
            for (const std::string& arg : companion.args) spawned.argv_utf16.push_back(secret_utf16_bytes(arg));
            spawn.companions.push_back(std::move(spawned));
        }
        for (const ports::InjectEntry& entry : launch.inject) {
            auto path = map(entry.path);
            if (!path) return fail(std::move(path.error()));
            state.injected_paths.emplace_back(*path, entry.path);
            spawn.inject.push_back(wh::InjectSpec{std::move(*path), entry.sha256, entry.strategy, entry.phase});
        }
        for (const NativePath& parked : launch.park) {
            auto path = map(parked);
            if (!path) return fail(std::move(path.error()));
            spawn.park_utf16.push_back(std::move(*path));
        }
        spawn.inject_timeout_ms = static_cast<u32>(scaled(kInjectTimeout, launch.multiplier).count());
        spawn.drain_timeout_ms = static_cast<u32>(scaled(kDrainTimeout, launch.multiplier).count());
        return spawn;
    }

    [[nodiscard]] game_channel::WinhostHandlers handlers(const StatePtr& state) {
        const WeakState weak = state;
        game_channel::WinhostHandlers out;
        out.configure = [weak](const game_channel::WinhostHello& hello) -> Result<wh::SpawnGame> {
            const StatePtr locked = weak.lock();
            if (!locked || !locked->spawn) return std::unexpected(internal_bug("WineSessionHost: a second winhost Hello"));
            REBOOT_LOG_AT(LogLevel::Info, Play, locked->session, "winhost {} (pid {}) connected", hello.build, hello.pid);
            wh::SpawnGame spawn = std::move(*locked->spawn);
            locked->spawn.reset();
            // The Welcome goes out once this returns; queued requests follow it.
            locked->strand.post([weak] {
                if (const StatePtr again = weak.lock()) on_welcomed(again);
            });
            return spawn;
        };
        out.on_event = [weak](game_channel::WinhostEvent event) {
            if (const StatePtr locked = weak.lock()) on_winhost_event(locked, std::move(event));
        };
        out.on_liveness = [weak](game_channel::PeerLiveness liveness) {
            if (const StatePtr locked = weak.lock())
                REBOOT_LOG_AT(LogLevel::Info, Play, locked->session, "winhost is {}",
                              liveness == game_channel::PeerLiveness::Responsive ? "responsive" : "unresponsive");
        };
        out.on_lost = [weak](Diagnostic error) {
            if (const StatePtr locked = weak.lock()) end(locked, std::move(error));
        };
        return out;
    }

    void watch_runner(const StatePtr& state) {
        const WeakState weak = state;
        const auto log_line = [weak](std::string_view line, bool) {
            const StatePtr locked = weak.lock();
            if (locked && *locked->wine_log) (*locked->wine_log)(locked->session, line);
        };
        state->runner_out.emplace(log_line);
        state->runner_err.emplace(log_line);
        Executor& strand_ref = strand;
        state->runner->on_stdout([weak, &strand_ref](std::span<const u8> bytes) {
            strand_ref.post([weak, bytes = std::vector<u8>(bytes.begin(), bytes.end())] {
                const StatePtr locked = weak.lock();
                if (locked && locked->runner_out) locked->runner_out->feed(bytes);
            });
        });
        state->runner->on_stderr([weak, &strand_ref](std::span<const u8> bytes) {
            strand_ref.post([weak, bytes = std::vector<u8>(bytes.begin(), bytes.end())] {
                const StatePtr locked = weak.lock();
                if (locked && locked->runner_err) locked->runner_err->feed(bytes);
            });
        });
        state->runner->on_exit([weak, &strand_ref](ports::ChildExit exit) {
            strand_ref.post([weak, exit] {
                if (const StatePtr locked = weak.lock()) on_runner_exit(locked, exit);
            });
        });
    }

    ports::IProcessLauncher& processes;
    ports::IRunnerPlatform& runner;
    game_channel::GameChannelListener& channel;
    TimerService& timers;
    Executor& strand;
    std::shared_ptr<WineLogLine> wine_log;
    std::map<SessionId, WineSessionSetup> staged;
};

WineSessionHost::WineSessionHost(WineSessionHostDeps deps) : impl_(std::make_unique<Impl>(deps)) {}

WineSessionHost::~WineSessionHost() = default;

Result<void> WineSessionHost::stage(SessionId session, WineSessionSetup setup) {
    if (impl_->staged.contains(session))
        return make_diag(ErrorDomain::Compat, msg::kSessionAlreadyStaged)
            .arg("session", format_uuid(session.value))
            .kind(ErrorKind::Conflict)
            .fail();
    impl_->staged.emplace(session, std::move(setup));
    return {};
}

void WineSessionHost::discard(SessionId session) noexcept { impl_->staged.erase(session); }

Result<std::unique_ptr<ports::IGameSession>> WineSessionHost::launch(const ports::SessionLaunch& launch,
                                                                     UniqueFunction<void(ports::SessionHostEvent)> on_event) {
    Impl& impl = *impl_;
    const auto staged = impl.staged.find(launch.session);
    if (staged == impl.staged.end())
        return make_diag(ErrorDomain::Compat, msg::kSessionNotStaged)
            .arg("session", format_uuid(launch.session.value))
            .kind(ErrorKind::NotFound)
            .fail();
    WineSessionSetup setup = std::move(staged->second);
    impl.staged.erase(staged);

    auto state = std::make_shared<SessionState>(impl.strand, impl.timers, impl.wine_log);
    state->session = launch.session;
    state->kind = setup.kind;
    state->paths = setup.paths;

    auto spawn = impl.spawn_game(*state, launch);
    if (!spawn) return std::unexpected(std::move(spawn.error()));
    state->spawn = std::move(*spawn);

    auto runner_vars = impl.runner_vars(setup, launch);
    if (!runner_vars) return std::unexpected(std::move(runner_vars.error()));
    auto ctl = impl.channel.ctl_url();
    if (!ctl) return std::unexpected(std::move(ctl.error()));
    auto peer = impl.channel.open_winhost(launch.session, launch.multiplier, impl.handlers(state));
    if (!peer) return std::unexpected(std::move(peer.error()));
    state->peer = std::move(*peer);

    setup.env.runner(*runner_vars)
        .channel(gc::kEnvCtl, std::move(*ctl))
        .channel_secret(gc::kEnvCtlToken, state->peer->token().env_value())
        .channel(gc::kEnvSession, format_uuid(launch.session.value))
        .channel(gc::kEnvRole, std::string(kWinhostRole));
    auto env = std::move(setup.env).build();
    if (!env) return std::unexpected(std::move(env.error()));

    const auto spawn_failed = [&](Diagnostic cause) {
        return make_diag(ErrorDomain::Compat, msg::kRunnerSpawnFailed)
            .arg("runner", runner_name(setup.kind))
            .cause(std::move(cause))
            .fail();
    };
    auto runner_launch = impl.runner.runner_launch(setup.layout, setup.prefix, setup.winhost_exe, env->copy());
    if (!runner_launch) return spawn_failed(std::move(runner_launch.error()));
    runner_launch->scope_name = std::string(kScopePrefix) + format_uuid(launch.session.value);
    const process::WipingLaunch wiping(std::move(*runner_launch));
    auto child = impl.processes.spawn(wiping.get());
    if (!child) return spawn_failed(std::move(child.error()));
    state->runner = std::move(*child);
    state->on_event = std::move(on_event);
    impl.watch_runner(state);
    return std::unique_ptr<ports::IGameSession>(std::make_unique<WineGameSession>(std::move(state)));
}

}  // namespace reboot::compat
