#pragma once

#include <chrono>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include "reboot/foundation/diag.hpp"
#include "reboot/foundation/function.hpp"
#include "reboot/foundation/native_path.hpp"
#include "reboot/foundation/net_types.hpp"
#include "reboot/foundation/paths.hpp"
#include "reboot/foundation/random.hpp"
#include "reboot/foundation/types.hpp"
#include "reboot/ports/file_system.hpp"
#include "reboot/ports/ipc.hpp"
#include "reboot/ports/net.hpp"
#include "reboot/ports/os_services.hpp"
#include "reboot/ports/platform_paths.hpp"
#include "reboot/ports/process.hpp"
#include "reboot/ports/runner.hpp"
#include "reboot/ports/secret_store.hpp"
#include "reboot/ports/session_host.hpp"
#include "reboot/testing/conformance_report.hpp"
#include "reboot/testing/conformance_waiter.hpp"

// One suite per port, run against the real adapters and the fakes alike so the fakes cannot drift.
// Suites check ErrorKind, never message ids, except the ids a port itself names.
namespace reboot::testing {

class IPortBinder;

struct ConformanceEnv {
    IConformanceWaiter& waiter;
    // A directory the suite may fill: a ScratchDir for real adapters, any path for the fakes.
    NativePath scratch;
    std::chrono::milliseconds budget{5000};
};

// Roots are absolute and lexically normal, the data root differs from the exe dir and is never
// inside the Velopack package dir, and velopack_package_dir is set exactly for Velopack installs.
[[nodiscard]] ConformanceReport run_platform_paths_conformance(const ports::IPlatformPaths& paths);

struct FileSystemConformanceHooks {
    // Makes a symlink for the remove_tree check, which is skipped without it.
    UniqueFunction<Result<void>(const NativePath& link, const NativePath& target)> make_symlink;
};

// atomic_replace round trip, replace over an existing file, keep_backup leaving <target>.bak, a
// missing parent failing, read_all of a missing file failing NotFound, a second non-waiting lock
// failing Conflict and succeeding after release, a HeldFile reading the bytes it opened, revision
// changing on write, owner-only directories and files, remove_tree not following a symlink.
[[nodiscard]] ConformanceReport run_file_system_conformance(ports::IFileSystem& fs, const ConformanceEnv& env,
                                                            FileSystemConformanceHooks hooks = {});

// A file written through `fs` is reported within the budget; nothing after the handle is destroyed.
[[nodiscard]] ConformanceReport run_file_watcher_conformance(ports::IFileWatcher& watcher, ports::IFileSystem& fs,
                                                             const ConformanceEnv& env);

// volumes() is not empty, volume_of(scratch) is one of them, free_bytes <= total_bytes.
[[nodiscard]] ConformanceReport run_disk_info_conformance(ports::IDiskInfo& disk, const ConformanceEnv& env);

// put, get, overwrite, erase and get-after-erase on keys unique to this run, binary values with
// NULs; an Unavailable store fails every call. It erases everything it wrote.
[[nodiscard]] ConformanceReport run_secret_store_conformance(ports::ISecretStore& store, IRandom& random);

// Launches the suite needs; each OS package writes them with its own shell (cmd.exe or /bin/sh).
struct ProcessConformanceSubject {
    ports::ProcessLaunch exits_with_7;
    // Copies stdin to stdout until EOF, then exits 0.
    ports::ProcessLaunch echoes_stdin;
    // One line to stderr, then exits 0.
    ports::ProcessLaunch writes_stderr;
    // Prints the value of REBOOT_CONFORMANCE_MARKER, then its working directory, one per line.
    ports::ProcessLaunch prints_marker_and_cwd;
    // Starts a grandchild, prints the grandchild's pid on a line, then both wait forever.
    ports::ProcessLaunch spawns_grandchild;
    UniqueFunction<bool(u32 pid)> process_exists;
};

// Exit code delivered exactly once, stdin to stdout intact, stderr kept apart, env and cwd reach
// the child, stdin EOF ends a child that waits on it, terminate_tree takes the grandchild too,
// is_alive and kill honour `created` so a reused pid is never touched.
[[nodiscard]] ConformanceReport run_process_launcher_conformance(ports::IProcessLauncher& launcher,
                                                                 ProcessConformanceSubject subject,
                                                                 const ConformanceEnv& env);

struct SessionHostConformanceSubject {
    // reboot-fake-game with a script that exits on its own after a while.
    ports::SessionLaunch launch;
    // A DLL to inject after launch; the inject check is skipped without one.
    std::optional<ports::InjectEntry> probe_dll;
};

// Spawned{Game} after launch, nothing runs before resume, Injected for the probe DLL, Exited with
// the game's code, stop() ending the tree within its grace, and a destroyed session ending it too.
[[nodiscard]] ConformanceReport run_session_host_conformance(ports::ISessionHost& host,
                                                             const SessionHostConformanceSubject& subject,
                                                             const ConformanceEnv& env);

struct RunnerConformanceSubject {
    ports::RunnerKind kind{};
    // Installed runtimes of that kind.
    ports::RuntimeDirs runtime;
    NativePath prefix;
    NativePath winhost_exe;
};

// `kind` is supported, its layout root lies inside the runtime and its entry inside one of the
// runtime dirs, runner_launch runs the entry with WINEPREFIX set to the prefix and winhost as its
// program, post_extract is idempotent on each dir.
[[nodiscard]] ConformanceReport run_runner_platform_conformance(ports::IRunnerPlatform& runner,
                                                                const RunnerConformanceSubject& subject);

// Bound TCP and UDP ports are owned by owner_pid(), and a released port has no owner.
[[nodiscard]] ConformanceReport run_port_inspector_conformance(ports::IPortInspector& inspector, IPortBinder& binder);

// With `expected_uid` the peer of a loopback pair has that uid; without it every call fails with
// platform.not_supported.
[[nodiscard]] ConformanceReport run_loopback_peer_inspector_conformance(ports::ILoopbackPeerInspector& inspector,
                                                                        IPortBinder& binder,
                                                                        std::optional<u32> expected_uid);

// "localhost" includes a loopback address, a name under .invalid fails, and `done` runs exactly
// once when the token is cancelled right after the call.
[[nodiscard]] ConformanceReport run_resolver_conformance(ports::IResolver& resolver, const ConformanceEnv& env);

struct IpcConformanceSubject {
    UniqueFunction<std::unique_ptr<ports::IIpcListener>()> make_listener;
    std::unique_ptr<ports::IIpcConnector> connector;
    // A fresh endpoint name per check.
    UniqueFunction<std::string()> make_endpoint;
    std::string self_user_id;
    // Absent where the runner cannot act as another OS user; those checks are then skipped.
    UniqueFunction<Result<std::unique_ptr<ports::IByteStream>>(const std::string& endpoint)> connect_as_other_user;
    UniqueFunction<Result<void>(const std::string& endpoint)> squat_as_other_user;
};

// Bytes both ways in order, a kIpcFrameCap-sized write intact, both ends seeing self_user_id,
// a second listener on a live endpoint failing (squatting), connect with nobody listening failing
// EngineUnavailable, close reaching the other end once, a closed listener accepting nothing, another
// user's client dropped, and an endpoint another user squats refused with ipc.endpoint_untrusted.
[[nodiscard]] ConformanceReport run_ipc_conformance(IpcConformanceSubject subject, const ConformanceEnv& env);

struct EngineStarterSubject {
    NativePath engine_exe;
    DataRoot root;
    // Whether an engine now answers on the root's endpoint.
    UniqueFunction<bool()> engine_reachable;
};

// Started or AlreadyRunning, an engine reachable within the EngineConnect deadline, and
// AlreadyRunning on a second call.
[[nodiscard]] ConformanceReport run_engine_starter_conformance(ports::IEngineStarter& starter,
                                                               EngineStarterSubject subject, const ConformanceEnv& env);

// capture() is stable across calls, display variable names are unique, allow_foreground of our own
// pid is harmless.
[[nodiscard]] ConformanceReport run_caller_context_conformance(ports::ICallerContextProbe& probe, u32 own_pid);

// Only checks that open nothing on screen: http and file URLs are refused InvalidInput, and trash
// moves a scratch file away.
[[nodiscard]] ConformanceReport run_shell_launcher_conformance(ports::IShellLauncher& shell, ports::IFileSystem& fs,
                                                               const ConformanceEnv& env);

// apply makes each kind Ours for `exe`, status is stable, remove makes it Absent and is idempotent.
// It changes the user's real registrations: CI runners only.
[[nodiscard]] ConformanceReport run_integration_registrar_conformance(ports::IIntegrationRegistrar& registrar,
                                                                      const NativePath& exe,
                                                                      std::span<const ports::IntegrationKind> kinds);

// Answers within the Wmi deadline; product names are non-empty and unique.
[[nodiscard]] ConformanceReport run_security_probe_conformance(ports::ISecurityProductProbe& probe,
                                                               const ConformanceEnv& env);

// Ids are non-empty and unique, every unmet one names a remediation message, an unknown id fails
// remediate.
[[nodiscard]] ConformanceReport run_prerequisite_probe_conformance(ports::IPrerequisiteProbe& probe);

// OS facts are non-empty and stable, and a CA bundle, when named, is an absolute path.
[[nodiscard]] ConformanceReport run_system_info_conformance(const ports::ISystemInfo& system);

// stage() of a missing package fails and leaves nothing staged; never applies.
[[nodiscard]] ConformanceReport run_update_applier_conformance(ports::IUpdateApplier& updater,
                                                               const ConformanceEnv& env);

struct HttpConformanceSubject {
    // Answers 200 with `ok_body`.
    std::string ok_url;
    std::vector<u8> ok_body;
    // Sends headers, then never the body.
    std::string stalled_url;
    // Serves `ok_body` and honours Range; the resume check is skipped when empty.
    std::string range_url;
};

// Headers before body, the body intact, on_done exactly once in every path: success, on_body_chunk
// returning false, cancel (Cancelled), a stall or total timeout, an unresolvable host.
[[nodiscard]] ConformanceReport run_http_transport_conformance(ports::IHttpTransport& transport,
                                                               const HttpConformanceSubject& subject,
                                                               const ConformanceEnv& env);

// `echo` is a server that echoes each stream and datagram back.
struct QuicConformanceSubject {
    ports::QuicConnectOptions echo;
    ports::QuicConnectOptions refused;
};

// on_connected before any data, stream bytes echoed in order with fin, a datagram echoed, a refused
// handshake ending in on_closed with an error and no on_connected, on_closed exactly once.
[[nodiscard]] ConformanceReport run_quic_transport_conformance(ports::IQuicTransport& transport,
                                                               const QuicConformanceSubject& subject,
                                                               const ConformanceEnv& env);

// Consecutive fills differ, are not all zero, and an empty fill is fine.
[[nodiscard]] ConformanceReport run_random_conformance(IRandom& random);

}  // namespace reboot::testing
