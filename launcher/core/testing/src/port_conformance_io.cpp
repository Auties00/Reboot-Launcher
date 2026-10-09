// The port suites that wait on callbacks: file system, watcher, processes, sessions, resolver,
// IPC, engine starter, shell, security probe, updater, HTTP and QUIC.
#include <algorithm>
#include <array>
#include <charconv>
#include <chrono>
#include <cstddef>
#include <filesystem>
#include <memory>
#include <mutex>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

#include "guarded.hpp"
#include "messages.hpp"
#include "reboot/foundation/cancel.hpp"
#include "reboot/foundation/framing.hpp"
#include "reboot/foundation/paths.hpp"
#include "reboot/foundation/text.hpp"
#include "reboot/testing/port_conformance.hpp"

namespace reboot::testing {

using namespace std::chrono_literals;

namespace {

// How long a suite watches for something that must not happen.
constexpr std::chrono::milliseconds kQuietWindow{300};

[[nodiscard]] std::span<const u8> as_bytes(std::string_view text) {
    return {reinterpret_cast<const u8*>(text.data()), text.size()};
}

[[nodiscard]] std::string as_text(std::span<const u8> bytes) { return {bytes.begin(), bytes.end()}; }

[[nodiscard]] std::chrono::milliseconds quiet(const ConformanceEnv& env) { return std::min(env.budget, kQuietWindow); }

[[nodiscard]] std::string without_cr(std::string text) {
    std::erase(text, '\r');
    return text;
}

[[nodiscard]] std::vector<std::string> lines_of(const std::string& text) {
    std::vector<std::string> lines;
    std::size_t start = 0;
    while (start < text.size()) {
        std::size_t end = text.find('\n', start);
        if (end == std::string::npos) end = text.size();
        std::string line = without_cr(text.substr(start, end - start));
        lines.push_back(std::move(line));
        start = end + 1;
    }
    return lines;
}

[[nodiscard]] bool same_dir(const NativePath& a, const NativePath& b) {
    std::error_code error;
    if (std::filesystem::equivalent(a, b, error) && !error) return true;
    return is_inside(a, b) && is_inside(b, a);
}

// --- file system --------------------------------------------------------------------------------

[[nodiscard]] Result<std::vector<u8>> read_held(ports::HeldFile& held) {
    std::vector<u8> out;
    std::array<u8, 7> chunk{};
    for (;;) {
        auto got = held.read(chunk);
        if (!got) return std::unexpected(std::move(got.error()));
        if (*got == 0) return out;
        out.insert(out.end(), chunk.begin(), chunk.begin() + static_cast<std::ptrdiff_t>(*got));
    }
}

}  // namespace

ConformanceReport run_file_system_conformance(ports::IFileSystem& fs, const ConformanceEnv& env,
                                              FileSystemConformanceHooks hooks) {
    ConformanceReport report("file_system");
    const NativePath dir = env.scratch / "fs-conformance";
    if (!report.expect_ok("create_dirs_owner_only", fs.create_dirs_owner_only(dir))) return report;

    const NativePath file = dir / "document.json";
    const NativePath backup = dir / "document.json.bak";
    const std::vector<u8> first{'{', '}', 0x00, 0xFF};
    const std::vector<u8> second{'[', ']', '\n'};
    const std::vector<u8> third{'"', 'x', '"'};

    if (report.expect_ok("atomic_replace a new file", fs.atomic_replace(file, first, false))) {
        const auto read = fs.read_all(file);
        report.expect("read_all returns what was written", read && *read == first);
    }
    if (report.expect_ok("atomic_replace over an existing file", fs.atomic_replace(file, second, false))) {
        const auto read = fs.read_all(file);
        report.expect("the replacement is read back", read && *read == second);
    }
    if (report.expect_ok("atomic_replace keeping a backup", fs.atomic_replace(file, third, true))) {
        const auto read = fs.read_all(file);
        const auto kept = fs.read_all(backup);
        report.expect("the new bytes are in place", read && *read == third);
        report.expect("keep_backup leaves the previous file as <target>.bak", kept && *kept == second);
    }
    report.expect("a missing parent fails atomic_replace", !fs.atomic_replace(dir / "missing" / "x.json", first, false));
    report.expect_error("read_all of a missing file fails NotFound", fs.read_all(dir / "absent.json"), ErrorKind::NotFound);

    const NativePath lock_path = dir / "conformance.lock";
    if (auto held = fs.lock_exclusive(lock_path, false); report.expect_ok("lock_exclusive", held)) {
        report.expect_error("a second non-waiting lock fails Conflict", fs.lock_exclusive(lock_path, false),
                            ErrorKind::Conflict);
        held->release();
        auto again = fs.lock_exclusive(lock_path, false);
        report.expect_ok("the lock is free after release", again);
    }

    if (auto opened = fs.open_deny_write(file); report.expect_ok("open_deny_write", opened)) {
        const auto read = read_held(*opened);
        report.expect("a HeldFile reads the bytes it opened", read && *read == third);
    }

    const auto before = fs.revision(file);
    if (report.expect_ok("revision", before) && report.expect_ok("rewrite", fs.atomic_replace(file, first, false))) {
        const auto after = fs.revision(file);
        report.expect("revision changes on write", after && *after != *before);
    }

    if (const auto tail = fs.read_shared(file, 1, 2); report.expect_ok("read_shared", tail)) {
        const auto current = fs.revision(file);
        report.expect("read_shared reads the range from the offset",
                      tail->bytes == std::vector<u8>(first.begin() + 1, first.begin() + 3));
        report.expect("read_shared reports the file's revision", current && tail->revision.file_id == current->file_id &&
                                                                     tail->revision.size == first.size());
    }
    if (const auto past = fs.read_shared(file, first.size() + 10, 16); report.expect_ok("read_shared past the end", past))
        report.expect("read_shared past the end reads nothing", past->bytes.empty());
    report.expect_error("read_shared of a missing file fails NotFound", fs.read_shared(dir / "absent.json", 0, 16),
                        ErrorKind::NotFound);

    report.expect_ok("create_dirs_owner_only nests", fs.create_dirs_owner_only(dir / "private" / "nested"));
    report.expect_ok("restrict_to_owner on a directory", fs.restrict_to_owner(dir / "private"));
    report.expect_ok("restrict_to_owner on a file", fs.restrict_to_owner(file));

    if (hooks.make_symlink) {
        const NativePath outside = dir / "outside";
        const NativePath tree = dir / "tree";
        const bool ready = report.expect_ok("prepare a link target", fs.create_dirs_owner_only(outside)) &&
                           report.expect_ok("prepare a kept file", fs.atomic_replace(outside / "keep", first, false)) &&
                           report.expect_ok("prepare the tree", fs.create_dirs_owner_only(tree)) &&
                           report.expect_ok("make a symlink", hooks.make_symlink(tree / "link", outside));
        if (ready) {
            report.expect_ok("remove_tree", fs.remove_tree(tree));
            const auto kept = fs.read_all(outside / "keep");
            report.expect("remove_tree does not follow a symlink", kept && *kept == first);
            report.expect("remove_tree removes the tree itself", !fs.revision(tree));
        }
    } else {
        report.skip("remove_tree does not follow a symlink", "no make_symlink hook");
    }

    report.expect_ok("remove_tree of a missing path succeeds", fs.remove_tree(dir / "never-made"));
    report.expect_ok("remove_tree cleans up", fs.remove_tree(dir));
    return report;
}

ConformanceReport run_file_watcher_conformance(ports::IFileWatcher& watcher, ports::IFileSystem& fs,
                                               const ConformanceEnv& env) {
    ConformanceReport report("file_watcher");
    const NativePath dir = env.scratch / "watch-conformance";
    if (!report.expect_ok("create the watched directory", fs.create_dirs_owner_only(dir))) return report;
    const NativePath target = dir / "watched.txt";

    auto seen = make_shared_state<std::vector<ports::FileChange>>();
    auto handle = watcher.watch(dir, [seen](ports::FileChange change) {
        seen->with([&](auto& changes) { changes.push_back(std::move(change)); });
    });
    if (!report.expect_ok("watch", handle)) return report;

    report.expect_ok("write a file", fs.atomic_replace(target, as_bytes("1"), false));
    const bool reported = env.waiter.wait_until(
        [&] {
            return seen->with([&](const auto& changes) {
                return std::ranges::any_of(changes, [&](const ports::FileChange& change) {
                    return change.path.filename() == target.filename();
                });
            });
        },
        env.budget);
    report.expect("a write is reported within the budget", reported);

    *handle = ports::WatchHandle{};
    // Changes already on their way when the handle went may still land; give them a moment.
    (void)env.waiter.wait_until([] { return false; }, quiet(env));
    const std::size_t count = seen->with([](const auto& changes) { return changes.size(); });
    report.expect_ok("write again", fs.atomic_replace(target, as_bytes("2"), false));
    const bool more = env.waiter.wait_until([&] { return seen->with([](const auto& c) { return c.size(); }) > count; },
                                            quiet(env));
    report.expect("nothing arrives after the handle is destroyed", !more);
    report.expect_ok("clean up", fs.remove_tree(dir));
    return report;
}

namespace {

// --- processes ----------------------------------------------------------------------------------

struct RunState {
    std::string out;
    std::string err;
    std::optional<ports::ChildExit> exit;
    int exits = 0;
};

struct Running {
    std::unique_ptr<ports::ChildProcess> child;
    Shared<RunState> state;
};

[[nodiscard]] Result<Running> start(ports::IProcessLauncher& launcher, ports::ProcessLaunch launch, ports::StdioMode stdio) {
    launch.stdio = stdio;
    auto child = launcher.spawn(launch);
    if (!child) return std::unexpected(std::move(child.error()));
    Running running{std::move(*child), make_shared_state<RunState>()};
    auto state = running.state;
    running.child->on_stdout([state](std::span<const u8> bytes) {
        state->with([&](RunState& s) { s.out.append(bytes.begin(), bytes.end()); });
    });
    running.child->on_stderr([state](std::span<const u8> bytes) {
        state->with([&](RunState& s) { s.err.append(bytes.begin(), bytes.end()); });
    });
    running.child->on_exit([state](ports::ChildExit exit) {
        state->with([&](RunState& s) {
            s.exit = exit;
            ++s.exits;
        });
    });
    return running;
}

[[nodiscard]] bool exited(const ConformanceEnv& env, const Running& running) {
    return env.waiter.wait_until([&] { return running.state->with([](const RunState& s) { return s.exit.has_value(); }); },
                                 env.budget);
}

}  // namespace

ConformanceReport run_process_launcher_conformance(ports::IProcessLauncher& launcher, ProcessConformanceSubject subject,
                                                   const ConformanceEnv& env) {
    ConformanceReport report("process_launcher");
    using ports::StdioMode;

    if (auto run = start(launcher, subject.exits_with_7, StdioMode::Capture); report.expect_ok("spawn", run)) {
        if (report.expect("the exit is delivered", exited(env, *run))) {
            const RunState state = run->state->copy();
            report.expect("the exit code is 7", state.exit->code == 7);
            (void)env.waiter.wait_until([] { return false; }, quiet(env));
            report.expect("the exit is delivered exactly once", run->state->copy().exits == 1);
        }
    }

    if (auto run = start(launcher, subject.echoes_stdin, StdioMode::ControlChannel); report.expect_ok("spawn an echo", run)) {
        const std::string payload = "reboot conformance\n" + std::string(4096, 'x') + "\n";
        run->child->write_stdin(as_bytes(payload));
        run->child->close_stdin();
        if (report.expect("stdin EOF ends a child that waits on it", exited(env, *run))) {
            const RunState state = run->state->copy();
            report.expect("stdin reaches stdout intact", without_cr(state.out) == payload,
                          std::to_string(state.out.size()) + " bytes came back");
            report.expect("the echo exits 0", state.exit->code == 0);
        }
    }

    if (auto run = start(launcher, subject.writes_stderr, StdioMode::Capture); report.expect_ok("spawn a stderr writer", run)) {
        if (report.expect("the stderr writer exits", exited(env, *run))) {
            const RunState state = run->state->copy();
            report.expect("stderr is kept apart from stdout", !state.err.empty() && state.out.empty());
        }
    }

    {
        const std::string marker = "reboot-conformance-marker-42";
        ports::ProcessLaunch launch = subject.prints_marker_and_cwd;
        launch.env.vars.emplace_back("REBOOT_CONFORMANCE_MARKER", marker);
        launch.cwd = env.scratch;
        if (auto run = start(launcher, std::move(launch), StdioMode::Capture); report.expect_ok("spawn the marker printer", run)) {
            if (report.expect("the marker printer exits", exited(env, *run))) {
                const std::vector<std::string> lines = lines_of(run->state->copy().out);
                report.expect("the environment reaches the child", !lines.empty() && lines[0] == marker);
                report.expect("the working directory reaches the child",
                              lines.size() >= 2 && same_dir(NativePath(lines[1]), env.scratch),
                              lines.size() >= 2 ? lines[1] : std::string("no second line"));
            }
        }
    }

    if (auto run = start(launcher, subject.spawns_grandchild, StdioMode::Capture); report.expect_ok("spawn a tree", run)) {
        u32 grandchild = 0;
        const bool printed = env.waiter.wait_until(
            [&] {
                const std::string out = run->state->copy().out;
                const std::size_t end = out.find('\n');
                if (end == std::string::npos) return false;
                const std::string line = without_cr(out.substr(0, end));
                const auto [ptr, ec] = std::from_chars(line.data(), line.data() + line.size(), grandchild);
                return ec == std::errc{} && grandchild != 0;
            },
            env.budget);
        if (report.expect("the grandchild's pid is printed", printed) && subject.process_exists) {
            report.expect("the grandchild runs", subject.process_exists(grandchild));
            report.expect_ok("terminate_tree", run->child->terminate_tree());
            report.expect("the child exits after terminate_tree", exited(env, *run));
            report.expect("terminate_tree takes the grandchild too",
                          env.waiter.wait_until([&] { return !subject.process_exists(grandchild); }, env.budget));
        } else {
            if (!subject.process_exists) report.skip("terminate_tree takes the grandchild too", "no process_exists hook");
            // The tree waits forever, so it never outlives the suite.
            (void)run->child->terminate_tree();
        }
    }

    if (auto run = start(launcher, subject.echoes_stdin, StdioMode::ControlChannel); report.expect_ok("spawn a waiting child", run)) {
        const u32 pid = run->child->pid();
        const auto created = run->child->created();
        const auto stale = created - std::chrono::hours{1};
        const auto alive = launcher.is_alive(pid, created);
        report.expect("is_alive with the right creation time", alive && *alive);
        const auto reused = launcher.is_alive(pid, stale);
        report.expect("is_alive refuses another creation time", reused && !*reused);
        (void)launcher.kill(pid, stale);
        report.expect("kill with another creation time touches nothing",
                      !env.waiter.wait_until([&] { return run->state->with([](const RunState& s) { return s.exit.has_value(); }); },
                                             quiet(env)));
        report.expect_ok("kill with the right creation time", launcher.kill(pid, created));
        report.expect("kill ends the child", exited(env, *run));
        const auto gone = launcher.is_alive(pid, created);
        report.expect("is_alive is false once it exited", gone && !*gone);
    }
    return report;
}

ConformanceReport run_session_host_conformance(ports::ISessionHost& host, SessionHostConformanceSubject subject,
                                               const ConformanceEnv& env) {
    ConformanceReport report("session_host");
    using Events = std::vector<ports::SessionHostEvent>;

    const auto launch = [&](Shared<Events> events) {
        return host.launch(subject.launch, [events](ports::SessionHostEvent event) {
            events->with([&](Events& all) { all.push_back(std::move(event)); });
        });
    };
    const auto has = [](const Shared<Events>& events, auto predicate) {
        return events->with([&](const Events& all) { return std::ranges::any_of(all, predicate); });
    };
    const auto game_spawned = [](const ports::SessionHostEvent& event) {
        const auto* spawned = std::get_if<ports::Spawned>(&event);
        return spawned != nullptr && spawned->role == ports::SessionRole::Game;
    };
    const auto game_exited = [](const ports::SessionHostEvent& event) {
        const auto* exit = std::get_if<ports::Exited>(&event);
        return exit != nullptr && exit->role == ports::SessionRole::Game;
    };
    const auto ran = [](const ports::SessionHostEvent& event) {
        return std::holds_alternative<ports::Output>(event) || std::holds_alternative<ports::Exited>(event);
    };

    {
        auto events = make_shared_state<Events>();
        auto session = launch(events);
        if (!report.expect_ok("launch", session)) return report;
        report.expect("Spawned{Game} follows launch",
                      env.waiter.wait_until([&] { return has(events, game_spawned); }, env.budget));
        report.expect("nothing runs before resume", !env.waiter.wait_until([&] { return has(events, ran); }, quiet(env)));
        if (subject.probe_dll) {
            if (report.expect_ok("inject the probe DLL", (*session)->inject(*subject.probe_dll))) {
                const auto injected = [](const ports::SessionHostEvent& event) {
                    const auto* result = std::get_if<ports::Injected>(&event);
                    return result != nullptr && result->ok;
                };
                report.expect("Injected reports the probe DLL",
                              env.waiter.wait_until([&] { return has(events, injected); }, env.budget));
            }
        } else {
            report.skip("Injected reports the probe DLL", "no probe DLL");
        }
        if (report.expect_ok("resume", (*session)->resume())) {
            report.expect("Exited{Game} follows the game's own exit",
                          env.waiter.wait_until([&] { return has(events, game_exited); }, env.budget));
            const bool coded = events->with([&](const Events& all) {
                return std::ranges::any_of(all, [](const ports::SessionHostEvent& event) {
                    const auto* exit = std::get_if<ports::Exited>(&event);
                    return exit != nullptr && exit->role == ports::SessionRole::Game && exit->code.has_value();
                });
            });
            report.expect("Exited carries the game's code", coded);
        }
    }

    {
        auto events = make_shared_state<Events>();
        auto session = launch(events);
        if (report.expect_ok("launch to stop", session) && report.expect_ok("resume to stop", (*session)->resume())) {
            const auto grace = default_deadline(OpKind::GracefulStop);
            (*session)->stop(grace);
            report.expect("stop() ends the tree within its grace",
                          env.waiter.wait_until([&] { return has(events, game_exited); }, grace + env.budget));
        }
    }

    if (subject.process_exists) {
        auto events = make_shared_state<Events>();
        auto session = launch(events);
        if (report.expect_ok("launch to destroy", session) && report.expect_ok("resume to destroy", (*session)->resume())) {
            u32 pid = 0;
            const bool spawned = env.waiter.wait_until(
                [&] {
                    return events->with([&](const Events& all) {
                        for (const auto& event : all)
                            if (const auto* game = std::get_if<ports::Spawned>(&event);
                                game != nullptr && game->role == ports::SessionRole::Game)
                                pid = game->pid;
                        return pid != 0;
                    });
                },
                env.budget);
            if (report.expect("the game to destroy spawned", spawned)) {
                session->reset();
                report.expect("a destroyed session ends its tree",
                              env.waiter.wait_until([&] { return !subject.process_exists(pid); }, env.budget));
            }
        }
    } else {
        report.skip("a destroyed session ends its tree", "no process_exists hook");
    }
    return report;
}

ConformanceReport run_resolver_conformance(ports::IResolver& resolver, const ConformanceEnv& env) {
    ConformanceReport report("resolver");
    struct Answers {
        int calls = 0;
        std::optional<Result<std::vector<IpAddress>>> result;
    };
    const auto ask = [&](std::string host, CancelToken token) {
        auto answers = make_shared_state<Answers>();
        resolver.resolve(std::move(host), std::move(token), [answers](Result<std::vector<IpAddress>> result) {
            answers->with([&](Answers& a) {
                ++a.calls;
                a.result = std::move(result);
            });
        });
        return answers;
    };
    const auto answered = [&](const Shared<Answers>& answers) {
        return env.waiter.wait_until([&] { return answers->with([](const Answers& a) { return a.calls > 0; }); }, env.budget);
    };

    if (auto local = ask("localhost", {}); report.expect("localhost is answered", answered(local))) {
        const Answers a = local->copy();
        const bool loopback = *a.result && std::ranges::any_of(**a.result, [](const IpAddress& ip) { return ip.is_loopback(); });
        report.expect("localhost includes a loopback address", loopback);
    }
    if (auto invalid = ask("reboot-conformance.invalid", {}); report.expect("an .invalid name is answered", answered(invalid))) {
        report.expect("a name under .invalid fails", !*invalid->copy().result);
    }
    CancelSource source;
    auto cancelled = ask("localhost", source.token());
    source.cancel(CancelReason::User);
    if (report.expect("a cancelled lookup is answered", answered(cancelled))) {
        (void)env.waiter.wait_until([] { return false; }, quiet(env));
        report.expect("done runs exactly once", cancelled->copy().calls == 1);
    }
    return report;
}

namespace {

struct Pipe {
    std::string received;
    int closes = 0;
};

void watch_stream(ports::IByteStream& stream, const Shared<Pipe>& pipe) {
    stream.on_read([pipe](std::span<const u8> bytes) { pipe->with([&](Pipe& p) { p.received.append(bytes.begin(), bytes.end()); }); });
    stream.on_close([pipe] { pipe->with([](Pipe& p) { ++p.closes; }); });
}

struct Accepted {
    std::vector<std::unique_ptr<ports::IByteStream>> streams;
    std::vector<Shared<Pipe>> pipes;
};

[[nodiscard]] Result<void> listen_watched(ports::IIpcListener& listener, const std::string& endpoint,
                                          const Shared<Accepted>& accepted) {
    return listener.listen(endpoint, [accepted](std::unique_ptr<ports::IByteStream> stream) {
        auto pipe = make_shared_state<Pipe>();
        // Watched before it is stored, so nothing the client sends early is missed.
        watch_stream(*stream, pipe);
        accepted->with([&](Accepted& a) {
            a.streams.push_back(std::move(stream));
            a.pipes.push_back(std::move(pipe));
        });
    });
}

}  // namespace

ConformanceReport run_ipc_conformance(IpcConformanceSubject subject, const ConformanceEnv& env) {
    ConformanceReport report("ipc");
    const std::string endpoint = subject.make_endpoint();
    auto listener = subject.make_listener();
    auto accepted = make_shared_state<Accepted>();
    if (!report.expect_ok("listen", listen_watched(*listener, endpoint, accepted))) return report;

    auto client = subject.connector->connect(endpoint, default_deadline(OpKind::EngineConnect));
    if (report.expect_ok("connect", client)) {
        auto client_pipe = make_shared_state<Pipe>();
        watch_stream(**client, client_pipe);
        const bool got = env.waiter.wait_until([&] { return accepted->with([](const Accepted& a) { return !a.streams.empty(); }); },
                                               env.budget);
        if (report.expect("the listener accepts the client", got)) {
            auto server_pipe = accepted->with([](Accepted& a) { return a.pipes.front(); });
            ports::IByteStream* server = accepted->with([](Accepted& a) { return a.streams.front().get(); });

            (*client)->write(as_bytes("abc"));
            (*client)->write(as_bytes("def"));
            const bool forward = env.waiter.wait_until(
                [&] { return server_pipe->with([](const Pipe& p) { return p.received.size() >= 6; }); }, env.budget);
            report.expect("client bytes arrive in order", forward && server_pipe->copy().received == "abcdef");
            server->write(as_bytes("xyz"));
            const bool back = env.waiter.wait_until(
                [&] { return client_pipe->with([](const Pipe& p) { return p.received.size() >= 3; }); }, env.budget);
            report.expect("engine bytes arrive in order", back && client_pipe->copy().received == "xyz");

            std::vector<u8> big(kIpcFrameCap);
            for (std::size_t i = 0; i < big.size(); ++i) big[i] = static_cast<u8>(i * 31 + 7);
            server_pipe->with([](Pipe& p) { p.received.clear(); });
            (*client)->write(big);
            const bool whole = env.waiter.wait_until(
                [&] { return server_pipe->with([&](const Pipe& p) { return p.received.size() >= big.size(); }); },
                env.budget * 4);
            report.expect("a kIpcFrameCap-sized write arrives intact",
                          whole && server_pipe->with([&](const Pipe& p) {
                              return p.received.size() == big.size() && std::ranges::equal(p.received, big, [](char c, u8 b) {
                                         return static_cast<u8>(c) == b;
                                     });
                          }));

            report.expect("the client sees self_user_id", (*client)->peer().user_id == subject.self_user_id);
            report.expect("the engine sees self_user_id", server->peer().user_id == subject.self_user_id);

            auto squatter = subject.make_listener();
            report.expect("a second listener on a live endpoint fails", !squatter->listen(endpoint, [](auto) {}));

            (*client)->close();
            const bool closed = env.waiter.wait_until([&] { return server_pipe->with([](const Pipe& p) { return p.closes > 0; }); },
                                                      env.budget);
            report.expect("close reaches the other end", closed);
            (void)env.waiter.wait_until([] { return false; }, quiet(env));
            report.expect("close reaches each end at most once",
                          server_pipe->copy().closes == 1 && client_pipe->copy().closes <= 1);
        }
    }

    listener->close();
    const auto after_close = subject.connector->connect(endpoint, 1s);
    report.expect("a closed listener accepts nothing",
                  !after_close || !env.waiter.wait_until(
                                      [&] { return accepted->with([](const Accepted& a) { return a.streams.size() > 1; }); },
                                      quiet(env)));
    report.expect_error("connect with nobody listening fails EngineUnavailable",
                        subject.connector->connect(subject.make_endpoint(), 1s), ErrorKind::EngineUnavailable);

    if (subject.connect_as_other_user) {
        const std::string guarded = subject.make_endpoint();
        auto other_listener = subject.make_listener();
        auto other_accepted = make_shared_state<Accepted>();
        if (report.expect_ok("listen for the other user", listen_watched(*other_listener, guarded, other_accepted))) {
            auto stranger = subject.connect_as_other_user(guarded);
            const bool handed_over = env.waiter.wait_until(
                [&] { return other_accepted->with([](const Accepted& a) { return !a.streams.empty(); }); }, quiet(env));
            report.expect("another user's client is dropped", !handed_over);
        }
    } else {
        report.skip("another user's client is dropped", "the runner cannot act as another user");
    }

    if (subject.squat_as_other_user) {
        const std::string squatted = subject.make_endpoint();
        if (report.expect_ok("squat as another user", subject.squat_as_other_user(squatted))) {
            const auto refused = subject.connector->connect(squatted, 1s);
            report.expect("an endpoint another user squats is refused with ipc.endpoint_untrusted",
                          !refused && refused.error().is(msg::kEndpointUntrusted));
        }
    } else {
        report.skip("an endpoint another user squats is refused", "the runner cannot act as another user");
    }
    return report;
}

ConformanceReport run_engine_starter_conformance(ports::IEngineStarter& starter, EngineStarterSubject subject,
                                                 const ConformanceEnv& env) {
    ConformanceReport report("engine_starter");
    const auto first = starter.ensure_started(subject.engine_exe, subject.root);
    if (!report.expect_ok("ensure_started", first)) return report;
    report.expect("the first call starts or finds an engine",
                  *first == ports::StartResult::Started || *first == ports::StartResult::AlreadyRunning);
    report.expect("an engine is reachable within the EngineConnect deadline",
                  env.waiter.wait_until(std::move(subject.engine_reachable), default_deadline(OpKind::EngineConnect)));
    const auto second = starter.ensure_started(subject.engine_exe, subject.root);
    report.expect("a second call finds it running", second && *second == ports::StartResult::AlreadyRunning);
    return report;
}

ConformanceReport run_shell_launcher_conformance(ports::IShellLauncher& shell, ports::IFileSystem& fs,
                                                 const ConformanceEnv& env) {
    ConformanceReport report("shell_launcher");
    report.expect_error("an http URL is refused", shell.open_url("http://example.invalid/"), ErrorKind::InvalidInput);
    report.expect_error("a file URL is refused", shell.open_url("file:///etc/hosts"), ErrorKind::InvalidInput);
    const NativePath doomed = env.scratch / "trash-conformance.txt";
    if (report.expect_ok("write a scratch file", fs.atomic_replace(doomed, as_bytes("trash me"), false))) {
        report.expect_ok("trash", shell.trash(doomed));
        report.expect_error("trash moves the file away", fs.read_all(doomed), ErrorKind::NotFound);
    }
    return report;
}

ConformanceReport run_security_probe_conformance(ports::ISecurityProductProbe& probe, const ConformanceEnv&) {
    ConformanceReport report("security_probe");
    // Real time on purpose: the probe is synchronous and its bound is the Wmi deadline.
    const auto began = std::chrono::steady_clock::now();
    const auto answer = probe.probe();
    const auto took = std::chrono::steady_clock::now() - began;
    report.expect("answers within the Wmi deadline", took <= default_deadline(OpKind::Wmi));
    if (!report.expect_ok("probe", answer) || !*answer) return report;
    const std::vector<std::string>& names = (*answer)->names;
    report.expect("product names are not empty", std::ranges::none_of(names, [](const std::string& name) { return name.empty(); }));
    std::vector<std::string> sorted = names;
    std::ranges::sort(sorted);
    report.expect("product names are unique", std::ranges::adjacent_find(sorted) == sorted.end());
    return report;
}

ConformanceReport run_update_applier_conformance(ports::IUpdateApplier& updater, const ConformanceEnv& env) {
    ConformanceReport report("update_applier");
    report.expect("stage() of a missing package fails", !updater.stage(env.scratch / "missing-package.nupkg"));
    return report;
}

namespace {

struct Transfer {
    std::optional<u32> status;
    bool body_before_headers = false;
    std::vector<u8> body;
    int done = 0;
    std::optional<Result<ports::HttpStatus>> result;
};

[[nodiscard]] Shared<Transfer> perform(ports::IHttpTransport& transport, ports::HttpRequest request, CancelToken token,
                                       bool abort_on_body = false) {
    auto transfer = make_shared_state<Transfer>();
    ports::HttpCallbacks callbacks;
    callbacks.on_headers = [transfer](ports::HttpStatus status, const std::vector<ports::HttpHeader>&) {
        transfer->with([&](Transfer& t) { t.status = status.code; });
    };
    callbacks.on_body_chunk = [transfer, abort_on_body](std::span<const u8> chunk) {
        transfer->with([&](Transfer& t) {
            if (!t.status) t.body_before_headers = true;
            t.body.insert(t.body.end(), chunk.begin(), chunk.end());
        });
        return !abort_on_body;
    };
    callbacks.on_done = [transfer](Result<ports::HttpStatus> result) {
        transfer->with([&](Transfer& t) {
            ++t.done;
            t.result = std::move(result);
        });
    };
    transport.perform(std::move(request), std::move(callbacks), std::move(token));
    return transfer;
}

[[nodiscard]] ports::HttpRequest get(std::string url) {
    ports::HttpRequest request;
    request.method = "GET";
    request.url = std::move(url);
    request.connect_timeout = 10s;
    request.total_timeout = 20s;
    return request;
}

}  // namespace

ConformanceReport run_http_transport_conformance(ports::IHttpTransport& transport, const HttpConformanceSubject& subject,
                                                 const ConformanceEnv& env) {
    ConformanceReport report("http_transport");
    const auto finished = [&](const Shared<Transfer>& transfer, std::chrono::milliseconds budget) {
        return env.waiter.wait_until([&] { return transfer->with([](const Transfer& t) { return t.done > 0; }); }, budget);
    };
    const auto once = [&](const Shared<Transfer>& transfer) {
        (void)env.waiter.wait_until([] { return false; }, quiet(env));
        return transfer->copy().done == 1;
    };

    if (auto ok = perform(transport, get(subject.ok_url), {}); report.expect("a GET finishes", finished(ok, env.budget))) {
        const Transfer t = ok->copy();
        report.expect("it succeeds", *t.result && (*t.result)->code == 200);
        report.expect("headers come before the body", t.status && !t.body_before_headers);
        report.expect("the body is intact", t.body == subject.ok_body);
        report.expect("on_done runs exactly once on success", once(ok));
    }

    if (auto aborted = perform(transport, get(subject.ok_url), {}, true);
        report.expect("an aborted GET finishes", finished(aborted, env.budget))) {
        report.expect("on_body_chunk returning false fails the transfer", !*aborted->copy().result);
        report.expect("on_done runs exactly once on abort", once(aborted));
    }

    {
        CancelSource source;
        auto cancelled = perform(transport, get(subject.stalled_url), source.token());
        source.cancel(CancelReason::User);
        if (report.expect("a cancelled GET finishes", finished(cancelled, env.budget))) {
            const auto result = cancelled->copy().result;
            report.expect("a cancel ends with Cancelled", !*result && result->error().kind == ErrorKind::Cancelled);
            report.expect("on_done runs exactly once on cancel", once(cancelled));
        }
    }

    {
        ports::HttpRequest request = get(subject.stalled_url);
        request.stall = ports::StallPolicy{1024, 1s};
        request.total_timeout = 3s;
        auto stalled = perform(transport, std::move(request), {});
        if (report.expect("a stalled GET is ended", finished(stalled, env.budget + 3s))) {
            const auto result = stalled->copy().result;
            report.expect("a stall or timeout fails without Cancelled",
                          !*result && result->error().kind != ErrorKind::Cancelled);
            report.expect("on_done runs exactly once on a stall", once(stalled));
        }
    }

    if (auto unresolved = perform(transport, get("https://reboot-conformance.invalid/"), {});
        report.expect("a GET to an unresolvable host finishes", finished(unresolved, env.budget))) {
        report.expect("an unresolvable host fails", !*unresolved->copy().result);
        report.expect("on_done runs exactly once for an unresolvable host", once(unresolved));
    }

    if (subject.range_url.empty()) {
        report.skip("Range resumes the body", "no range URL");
    } else {
        const std::size_t from = subject.ok_body.size() / 2;
        ports::HttpRequest request = get(subject.range_url);
        request.headers.push_back({"Range", "bytes=" + std::to_string(from) + "-"});
        if (auto ranged = perform(transport, std::move(request), {});
            report.expect("a ranged GET finishes", finished(ranged, env.budget))) {
            const Transfer t = ranged->copy();
            report.expect("a range answers 206", *t.result && (*t.result)->code == 206);
            report.expect("a range sends the tail",
                          std::ranges::equal(t.body, std::span(subject.ok_body).subspan(from)));
        }
    }
    return report;
}

namespace {

struct QuicState {
    bool connected = false;
    bool data_before_connected = false;
    std::vector<u8> stream_data;
    bool fin = false;
    std::vector<std::vector<u8>> datagrams;
    int closes = 0;
    std::optional<Diagnostic> close_error;
};

[[nodiscard]] ports::QuicCallbacks watch_quic(const Shared<QuicState>& state) {
    ports::QuicCallbacks callbacks;
    callbacks.on_connected = [state] { state->with([](QuicState& s) { s.connected = true; }); };
    callbacks.on_stream_data = [state](u64, std::span<const u8> data, bool fin) {
        state->with([&](QuicState& s) {
            if (!s.connected) s.data_before_connected = true;
            s.stream_data.insert(s.stream_data.end(), data.begin(), data.end());
            s.fin = s.fin || fin;
        });
    };
    callbacks.on_datagram = [state](std::span<const u8> data) {
        state->with([&](QuicState& s) { s.datagrams.emplace_back(data.begin(), data.end()); });
    };
    callbacks.on_closed = [state](std::optional<Diagnostic> error) {
        state->with([&](QuicState& s) {
            ++s.closes;
            s.close_error = std::move(error);
        });
    };
    return callbacks;
}

}  // namespace

ConformanceReport run_quic_transport_conformance(ports::IQuicTransport& transport, const QuicConformanceSubject& subject,
                                                 const ConformanceEnv& env) {
    ConformanceReport report("quic_transport");
    {
        auto state = make_shared_state<QuicState>();
        auto connection = transport.open_connection(subject.echo, watch_quic(state));
        if (report.expect_ok("open_connection", connection)) {
            const bool connected =
                env.waiter.wait_until([&] { return state->with([](const QuicState& s) { return s.connected; }); }, env.budget);
            if (report.expect("on_connected arrives", connected)) {
                const std::string payload = "reboot quic conformance";
                const auto stream = (*connection)->open_stream();
                if (report.expect_ok("open_stream", stream) &&
                    report.expect_ok("send", (*connection)->send(*stream, {payload.begin(), payload.end()}, true))) {
                    const bool echoed = env.waiter.wait_until(
                        [&] { return state->with([&](const QuicState& s) { return s.fin && s.stream_data.size() >= payload.size(); }); },
                        env.budget);
                    report.expect("stream bytes are echoed in order with fin",
                                  echoed && as_text(state->copy().stream_data) == payload);
                }
                const std::vector<u8> datagram{'d', 'g', 0x00, 0xFF};
                if (report.expect_ok("send_datagram", (*connection)->send_datagram(datagram))) {
                    const bool echoed = env.waiter.wait_until(
                        [&] { return state->with([](const QuicState& s) { return !s.datagrams.empty(); }); }, env.budget);
                    report.expect("a datagram is echoed", echoed && state->copy().datagrams.front() == datagram);
                }
                report.expect("on_connected comes before any data", !state->copy().data_before_connected);
            }
            (*connection)->close(0);
            (void)env.waiter.wait_until([] { return false; }, quiet(env));
            report.expect("on_closed runs at most once", state->copy().closes <= 1);
        }
    }
    {
        auto state = make_shared_state<QuicState>();
        auto connection = transport.open_connection(subject.refused, watch_quic(state));
        if (connection) {
            const bool closed =
                env.waiter.wait_until([&] { return state->with([](const QuicState& s) { return s.closes > 0; }); }, env.budget);
            const QuicState s = state->copy();
            report.expect("a refused handshake ends in on_closed with an error", closed && s.close_error.has_value());
            report.expect("a refused handshake never connects", !s.connected);
            (void)env.waiter.wait_until([] { return false; }, quiet(env));
            report.expect("on_closed runs exactly once", state->copy().closes == 1);
        } else {
            report.expect("a refused handshake fails", true);
        }
    }
    return report;
}

}  // namespace reboot::testing
