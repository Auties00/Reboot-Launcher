# Reboot Launcher architecture

Status: the architecture of the launcher rewrite, kept in the repo next to the code. Package writers follow this document and its Conventions section. Where an older design note disagrees with this document, this document wins. Owner decisions of 2026-10-14 are applied: the engine daemon and the C ABI stay, a generated typed C++ SDK is the interface for C++ clients, nothing is promised across releases before v1 beyond the bootstrap subset, and the tree has the layout of §3.

## 0. Scope

The launcher is built in phases:

- **core**: portable domain logic, the resident engine, private IPC, the public C ABI client library, the C++ SDK over it and the test kit.
- **core-windows, core-macos, core-linux**: one adapter per OS port, plus the few executables that only exist on one OS.
- **payload**: what runs inside the game's process or Wine prefix: winhost today, our client DLL later.
- **backend** (phase 2), **game-server** (phase 3), **clients** and **cli** (phase 4).

The backend, the game server and our in-game client DLL appear in the core only as **contracts**. A contract is a header of message types, plus the engine-side adapter that starts, configures, observes and stops the program, plus a fake that speaks the protocol. Their internals are not designed here.

Not in scope yet:

- the Steam Deck Game Mode attach path (deferred, §17);
- a Windows firewall-rule feature;
- boot-time or session-0 hosting.

## 1. How the inputs were applied

### 1.1 Owner decisions

| # | Owner decision | Consequence in this design |
|---|---|---|
| 1 | Plain headers/.cpp in CMake targets. C++26 only where MSVC, Apple Clang/libc++ and GCC 15 all support it. The binary boundary is a flat C ABI; C++ clients use a generated header-only SDK over it | One static library per package. `reboot_client` is the only shared library. Its header `reboot/client.h` and the `reboot.api.v1` schema are the public surface; `rb::sdk` (§9.3) is the C++ interface to it. The C ABI exists for toolchain and CRT isolation, not for language count: the core builds `/MT`, C++26, CMake+vcpkg+Boost, while WinUI 3 C++/WinRT builds under MSBuild/NuGet with `/MD`, Swift needs C, and a Linux GUI wants a prebuilt `.so`. There are no named modules, no `import std`, no `std::inplace_vector`, no `std::flat_map`, no `std::jthread`/`std::stop_token` and no `std::execution`. A CI floor job builds every target with all three toolchains. |
| 2 | Play and host on all three OSes. Play runs the Windows game natively, or under a launcher-managed Wine, with our client DLL injected. Host runs our standalone game-server program | `compat`, `injection`, `game_channel` and the runner packages serve **play only**. `host` drives a native `reboot-game-server` child with no Wine, no prefix, no injection, no exe patch, no Xvfb and no `Graphics=null`. The game server is a C++26 program that does not run the Fortnite binary: the Unreal Engine source is available, so it reimplements what it needs. |
| 3 | A resident per-user engine owns sessions, the backend, stores and secrets. UIs are IPC clients. Sessions survive a UI closing. No boot-time or session-0 hosting | `engine` + `ipc` + `client`. The engine origins are OnDemand, ServiceManager and Foreground. There is no SCM service, no S4U task and no EngineTarget. |
| 4 | No backward compatibility with the old launcher (10.x) | Nothing imports, reads or cleans up 10.x settings, library, backend data, host identity, install, registry entries, URL scheme or Defender exclusion. There is no `migrate` package and no cleanup helper. The custom auth DLL mode (decision 5) is not 10.x compatibility and stays. |
| 5 | We own every DLL and the backend: no upstream stand-ins. A user custom auth DLL is kept through legacy fixed-origin mode | `NetMode::LegacyFixed` exposes the backend at 127.0.0.1:3551, plus :80 on Windows, for one session at a time, always at the Untested tier. The only user DLL slot is the custom auth DLL. There is no mirrored reboot.dll, no patched Lawin and no upstream provider. |
| 6 | Engine IPC and the engine-to-DLL control protocols are private. No API or ABI compatibility is promised until v1 | Private protocols are header-only C++ aggregates on the sb codec (`core/contracts`), not `.proto`. They need an exact build or protocol match, with no capability negotiation and no N-1 support. Before v1 the C ABI and the schema are promised only within one release; the frozen bootstrap subset (§8) is the only cross-build promise. |
| 7 | Phases: core now; then backend, game server, UIs and CLI | §10 defines the backend and game-server process contracts. Capabilities that belong to their internals are unassigned (§16). |
| 8 | The backend is a separate process for isolation and community mode, not for licence | §1.4. A clean-room backend is GPL-3.0-or-later like the rest of the launcher. |

### 1.2 Resolutions that define behaviour

| Conflict | Resolution used here |
|---|---|
| `OPENSSL_ia32cap` default | Off by default. It is a per-build opt-in from the catalog (`BuildFlags.openssl_ia32cap`) and is set in exactly one place: `process` `EnvBuilder`. |
| IPC frame cap | 16 MiB, held as `kIpcFrameCap` in the one framing header (`base/framing.hpp`). Child stdio uses `kChildFrameCap` = 4 MiB and game-control uses `kGameControlFrameCap` = 1 MiB, both in the same header. Bulk data travels as a `NativePath`. |
| Idle exit | "Resident" means the engine stays up while any session, backend lease or user pin, host publication or Detached op is live. **Only an OnDemand-origin engine** exits, after 5 minutes idle. ServiceManager and Foreground engines never idle-exit. |
| Linux toolchain | The language floor is GCC 15 (owner-1). The release builder image is an open item (§17). |
| Launch credential | `-AUTH_LOGIN=<account_id>@projectreboot.dev`. `-AUTH_TYPE=exchangecode` where the catalog flags the build; otherwise `epic` with a per-session, single-use, 5-minute secret. `legacy_argv` puts the real password in argv only for a custom auth DLL that targets a hosted backend. The literal `Rebooted` is never used. |
| Native Windows injection | Done in the engine through `core-windows/win32session` behind `ISessionHost`. winhost runs only under Wine. If the AV/Smart App Control spike, run against the windowless engine, shows a benefit, a winhost-based adapter can replace this behind the same port. |
| macOS runtime | One launcher-built CrossOver-source Wine 11.0 + DXMT runtime, used for **play only**. Gcenx is a spike control and is never shipped. |
| Boot-time Windows hosting | None in v1 (owner-3). |
| Endpoint name | Windows: `\\.\pipe\reboot-engine-<UserSID>-<hash16>`. POSIX: `<base>/reboot-launcher/<hash16>.sock`. `hash16` = the first 16 hex digits of sha256(canonical data root), always present. |
| Loopback redirect | Builds keep their sentinel targets. Our client DLL rewrites them to the engine front's per-session origin, `http://127.0.0.1:<os-assigned>/s/<session_key>/`. The embedded backend writes that origin into the hotfix ServerAddr and the ticket serviceUrl (it learns it through `ConfigureSession`). Ports 3551 and :80 exist only in LegacyFixed. |
| Environment lists | One `EnvBuilder`. Layers, in order: daemon base → client allow-list → per-profile pass-through → runner vars → `REBOOT_*` channel vars → deny-list, applied last, which merges every source. |
| Windows autostart | DETACHED + NEW_PROCESS_GROUP + BREAKAWAY first; then the installer-registered on-demand scheduled task; then `RB_E_ENGINE_CANNOT_DETACH`, which points to `reboot-engine run --foreground`. The Explorer step is not used. Autostart is refused from an elevated or non-interactive token. |
| Game-control protocols | One framing header and one set of shared messages (Ping, Pong, CommandResult, Unsupported, Log, WireDiagnostic). Each channel has its own role-scoped message set: client DLL, winhost, backend, game server. `LegacyOutputAdapter` emits the same typed events for sessions without our DLL. |
| macOS engine start | `SMAppService.agent` handles registration and privacy attribution. `launchctl kickstart` starts it on demand. If that fails, the result is `AgentRequiresApproval` ("open the app once"). The nested-helper LaunchServices path is dropped. |
| Host port shape | The socket-set model. Sockets come from the game server's `--describe` output. A profile has a `Pinned(P)` or `Auto(range)` port policy. |

### 1.3 Amendments to earlier design notes

Dropped by owner-2, owner-3 and owner-5: the server-DLL parts of the game-server and DLL-suite designs, the upstream reboot.dll, session-0 and service hosting, and the Xvfb, host-prefix, `Graphics=null` and host-runner parts of the compat-layer designs. Their launcher-facing requirements live in the game-server contract (§10.3): configurable sockets and a bound-port report; typed match and player events; operator commands, with the IP/CIDR allowlist and IP-first bans; drain; several hosts per machine; the duplicate-UniqueNetId rule; the no-fallback bind.

Amendments made by this document:

1. **Private protocols** are header-only aggregates, not protobuf. `.proto` remains only for the public `reboot.api.v1`.
2. **Match target** is pulled, not pushed. The backend calls `ResolveMatchTarget{account_id, playlist}` on the engine, so nothing has to be replayed after a backend restart and config.ini sync disappears.
3. **Launch credentials.** `MintExchangeCode` and `MintLaunchSecret` are merged into `MintLaunchCredential{kind}`.
4. **Per-session backend configuration.** `SetSessionOrigin` and `SetConsoleKey` are merged into `ConfigureSession`/`EndSession`.
5. **Defender.** No Defender exclusion is ever added, automatically or with consent. A quarantined payload gets guided remediation that names the AV product.
6. **Updaters.** Velopack on Windows and macOS. Sparkle is not used.
7. **Elevation.** No code path elevates.
8. **Hidden flag.** `hidden = (listing == Unlisted) || Restarting`, recomputed on every update.
9. **Backend placement.** The backend never runs inside the engine process (§1.4). Notes that say otherwise, or that give the licence as the reason, are superseded.
10. **Client languages.** There are no C#, Rust or Swift facades and no facade generators. C++ clients use `rb::sdk`; Swift uses the C ABI directly.

### 1.4 Backend placement and licence

The backend is a separate executable, `reboot-backend`, supervised by the engine. The reasons:

- **Isolation of network-facing code.** It parses untrusted HTTP, WebSocket/XMPP and MCP JSON from game clients, and from other machines with `backend.allow_lan`. Out of process, a parser bug cannot take down every play and host session, and decrypted secrets, launch credentials and Credential Manager or Keychain access stay out of its address space. A crash is a restart with backoff while the games keep running. It is the same user, so this limits damage; it is not a security sandbox.
- **Community mode.** `reboot-backend --mode=community` is a standalone server for many players, so the executable must exist anyway.
- **Cost.** The contract is about 230 lines and the engine glue about 1.0-1.3k lines; `ChildSupervisor` is shared with the game server.

Licence is not a reason. A clean-room backend is GPL-3.0-or-later like the rest of the launcher, and there is no GPL-3.0-only tier.

`launcher/backend/` is `reboot_backend_lib` (HTTP, XMPP, MCP, store) plus a `main` for `--control=stdio` and `--mode=community`. The lib depends only on base, foundation and contracts, never on an engine package. Its tests run the lib in-process; the engine conformance suite runs the real executable.

**Licence map.** Only the backend tier changes from the earlier licence decision:

- GPL-3.0-or-later: core, core-windows, core-macos, core-linux, backend, game-server, clients, cli, re and game-model. `clients/windows` adds a section 7 permission for the Windows App SDK and the MSVC runtime.
- payload: GPL-3.0-or-later plus a permission to combine it with the game process.
- Apache-2.0: the C ABI header, the `reboot.api.v1` schema and the client stub, which hold only code we wrote. Whether `core/sdk` joins this tier is open (§17).
- Epic-originated backend data: LicenseRef with no licence claimed, kept to the minimum.
- server-browser and web-frontend (outside `launcher/`): AGPL-3.0-or-later.

## 2. Runtime topology

```
WinUI 3 C++/WinRT / C++26 CLI / Linux GUI      SwiftUI
        | compile rb::sdk (header-only)            |
        '------------------+----------------------'
                           | links
 reboot_client (public C ABI) --private IPC (pipe / AF_UNIX)--> reboot-engine (one per OS user + data root)
                                                                 |- reboot-backend           stdio, contracts/backend.hpp       (phase 2)
                                                                 |- reboot-game-server x N   stdio, contracts/game_server.hpp   (phase 3)
                                                                 |- SessionFront 127.0.0.1:0  <- game HTTP/WS (rewritten by our DLL)
                                                                 |- GameChannel 127.0.0.1:0   <- rb_client.dll and reboot-winhost.exe
                                                                 |- play: Windows = Job + in-engine injection (win32session)
                                                                 |        macOS/Linux = runner -> reboot-winhost.exe -> game
                                                                 '- rbsb/1 over MsQuic: browse, join, publish
```

The engine owns every child.

- **Windows:** each play session gets one Job with KILL_ON_JOB_CLOSE, and each native child gets its own Job.
- **Wine (macOS and Linux):** winhost holds the in-prefix Job and ends it when its game-channel connection reaches EOF.
- **Native children on macOS and Linux:** each runs in its own process group and is killed by a stdin-EOF watchdog. On Linux, PDEATHSIG and a per-session `systemd-run --user --scope` are a backstop.

An engine crash therefore ends every session, by design. Nothing adopts orphaned children. At the next start, `runtime.json` lets the engine reap a recorded pid when its creation time matches. Publications drop out of the server browser through the rbsb TTL.

## 3. Directory layout

Directories are created only when they get content; there are no empty placeholders. Marked (planned) do not exist yet.

```
launcher/
  CMakeLists.txt  CMakePresets.json  vcpkg.json  vcpkg-configuration.json
  cmake/          RebootPackage.cmake  RebootWarnings.cmake  RebootFloor.cmake  RebootDagCheck.cmake  dag_check.py  floor_check.py
  schema/         buf.yaml  buf.gen.yaml  proto/reboot/api/v1/*.proto (reboot.api.v1, §9.1)
  l10n/           en.json: every core message template, generated by reboot-l10n
  ci/             linux.Dockerfile and the Wine, umu and miniupnpd gates
  docs/           this document
  core/
    base/         header-only lean base (§4.1)
    foundation/   FOUNDATION: runtime: executor, events, operations, log facade, paths, message registry
    ports/        FOUNDATION: every OS port interface + PlatformServices
    contracts/    FOUNDATION: private wire messages (engine IPC, backend, game server, client DLL, winhost)
    api/          header-only reboot.api.v1 types
    api_dispatch/ engine-only handler interfaces and dispatch
    sdk/          header-only typed C++ SDK over reboot_client (§9.3)
    posix/ logging/ trust/ storage/ ux/ net/ components/ catalog/ support/ builds/
    process/ secrets/ identity/ injection/ game_channel/ compat/ backend/ front/ gameserver/
    sessions/ browser/ publish/ host/ play/ updates/ integration/
    ipc/ engine/ client/ testing/
  core-windows/   platform/ ipc/ win32session/
  core-macos/     platform/ ipc/ runner/
  core-linux/     platform/ ipc/ runner/
  payload/
    winhost/      reboot-winhost.exe
    client/       rb_client.dll (planned)
  backend/        reboot-backend: reboot_backend_lib + main (planned, phase 2)
  game-server/    reboot-game-server (planned, phase 3)
  re/             reverse-engineering toolchain (planned)
  game-model/     generated per-build game data (planned)
  clients/
    windows/      WinUI 3 C++/WinRT (planned)
    macos/        SwiftUI over the C ABI (planned)
    linux/        toolkit undecided (planned)
  cli/            C++26 CLI (planned)
```

- **core**: everything portable, including the engine, the C ABI library and the SDK.
- **core-windows, core-macos, core-linux**: the OS adapters. `win32session` stays in core-windows because the engine uses it for native Windows play; winhost links it too.
- **payload**: everything that is injected into the game or runs inside its Wine prefix: winhost, our client DLL and any later server-side DLL. All are x64 MSVC `/MT`, built by the Windows job and pinned by sha256 in the manifest. The target names `reboot-winhost` and `reboot_os_windows_winhost` are unchanged by the move.
- **backend**: the embedded and community backend (§1.4, §10.2).
- **game-server**: the standalone C++26 game server (§10.3). It does not run the Fortnite binary.
- **re**: the reverse-engineering toolchain: a pak reader, a reflection dumper DLL and traffic capture. Nothing ships from it.
- **game-model**: generated per-build data (offsets, signatures, reflection and class data for each game build), consumed by the backend, the payload DLLs and the game server.
- **clients**: one GUI per OS. Each C++ client compiles `rb::sdk` with its own toolchain and CRT.
- **cli**: the C++26 CLI (CLI11 + FTXUI), with a documented `--json` output (§9.4).

The **foundation** is `core/base`, `core/foundation`, `core/ports` and `core/contracts`. Every package may use the foundation. A package may use another package only if its entry lists it under "Depends on".

## 4. Build

### 4.1 CMake

- The root `CMakeLists.txt` adds `../server-browser` with `SB_CLIENT_ONLY=ON`, which yields the INTERFACE target `sb_contract`. That target contains:
  - `wire/{buffer,codec,frame}`, with `StreamFramer` yielding the raw u64 frame type;
  - `core/{types,flat_map,features}`, with an MSVC-clean `features.hpp`;
  - `client/view_mirror.hpp`;
  - `version_bucket` and `valid_text`, in a header with no registry dependency.
- `reboot_add_package(<area> SOURCES … DEPS …)` creates the static library `reboot_<area>` with alias `reboot::<area>`. It also creates the test executable `reboot_<area>_tests` from `tests/*.cpp`, linking `reboot::testing` and Catch2. `DEPS` is the full allowed set: `reboot::foundation` is implicit, and CI fails on any include from a package outside the set.
- **Foundation targets:**
  - `reboot_base` (INTERFACE, header-only): the lean subset `reboot/base/{types, result_fwd, diag, version, version_bucket, native_path, utf, framing, secret, function, log_level}.hpp`, plus `sb_contract` and Boost.PFR. `diag.hpp` here holds the diagnostic data types and `DiagBuilder`, not the message registry. Every function these headers declare is defined in them, so a target that links only base needs no library.
  - `reboot_contracts` (INTERFACE, header-only) links base only, not the foundation include directory. It is usable from `/MT` payload executables, the backend and the game server.
  - `reboot_foundation` (STATIC) links base and holds the runtime: executor, events, operations, the log facade, paths, user requests, random, text and the `REBOOT_MESSAGE` registry (`reboot/foundation/diag.hpp`, which adds the registry, `exit_code_for` and `internal_bug` to the base diagnostics). Boost.Asio is not on its PUBLIC link: `net`, `front`, `game_channel`, `engine`, `testing` and the other packages that use it link it themselves.
  - `reboot_ports` (INTERFACE).
- **API targets:**
  - `reboot_api` (INTERFACE, header-only): the generated `reboot.api.v1` aggregates and enums, the method ids and `MethodTable`, the codec and `DecodeError` as plain data. It links base only and uses no message registry. The engine, the SDK and testing use it.
  - `reboot_api_dispatch` (STATIC, engine only): the generated handler interfaces, `dispatch.hpp`/`dispatch.cpp`, `CallContext`, the bootstrap id checks, the `api.*` messages and the `DecodeError` → `Diagnostic` conversion.
  - `reboot_sdk` (INTERFACE, header-only): §9.3.
- **Executables:**
  - `reboot-engine` = `core/engine/main.cpp` + `reboot_engine` + `reboot::platform`.
  - `reboot_client` (SHARED) = `core/client` + `reboot::client_platform`.
  - Windows only: `reboot-winhost` (`payload/winhost`, x64 `/MT`).
  - Tests: `reboot-fake-backend`, `reboot-fake-game-server`, `reboot-fake-game` (Windows test exe).
  - Tools: `reboot-l10n` (`core/ux/tools`) renders and checks `l10n/en.json`; `reboot-data-root-probe` (`core/engine/gates`, Windows and macOS) serves the data root gate (§14).
- **Per-OS aliases:**
  - `reboot::platform` → `reboot_os_<os>_platform` + `reboot_os_<os>_ipc` (+ `reboot_os_<os>_runner` on macOS/Linux).
  - `reboot::client_platform` → `reboot_os_<os>_ipc`.
- **Third-party (vcpkg, baseline shared with server-browser):**
  - boost-asio, boost-beast, boost-json, boost-pfr, boost-container, boost-unordered;
  - openssl 3 (Ed25519, RAND, TLS);
  - curl (Schannel on Windows, OpenSSL elsewhere);
  - msquic (OpenSSL overlay);
  - libarchive, miniupnpc, libnatpmp;
  - velopack (Windows and macOS);
  - catch2.

  There is no protobuf runtime in any C++ target. `protoc` and `buf` are build-time tools only.

### 4.2 DAG rules (CI, from `cmake --graphviz` and include scanning)

1. The foundation depends only on `sb_contract`, Boost and the standard library. It includes no OS header, except in `foundation/src/random_os.cpp` (the CSPRNG). `base` and `contracts` reach nothing outside base, contracts and `sb_contract`.
2. Domain packages depend on the foundation and on their listed packages. They never depend on an OS module or on `posix`.
3. OS packages depend only on the foundation (and on `posix` for macOS and Linux, and on `win32session` for the Windows platform). They never depend on a domain package.
4. **Lean consumers** reach only base and contracts: the payload (winhost now, the client DLL and any server-side DLL later, plus `win32session`, which winhost links), the game server, and the SDK. The SDK may also reach `reboot/client.h` and the api types. None of them links `reboot_foundation`, Boost.Asio or a message registry.
5. `reboot_client` reaches only `foundation`, `contracts`, `ipc` and `reboot::client_platform`. It never reaches `api`, a domain package, OpenSSL, curl, MsQuic or libarchive. CI checks the imports of the built library.
6. `api_dispatch` is linked only by the engine and its tests.

## 5. Foundation summary

`core/base` holds the value types every program shares (ids, versions, paths, diagnostics as data, framing, secrets); `core/foundation` holds the runtime the core packages share. Key semantics:

- **Errors.** `Result<T> = std::expected<T, Diagnostic>`.
  - Core never returns user-facing text. A `Diagnostic` carries a stable `MessageId` (declared once with `REBOOT_MESSAGE(id, "English {arg}")`) and its typed args.
  - Typed domain errors convert to a `Diagnostic` at the service boundary.
  - Background failures end as `Outcome::Failed`. `internal.bug` is only for real bugs, caught at thread boundaries.
- **Threads.** The threads are:
  - one engine **Strand**, which owns all domain state;
  - one Asio I/O thread for IPC, the front, the game channel and the net adapters;
  - the `WorkerPool` for blocking work;
  - the logger writer;
  - MsQuic's own threads.

  Adapters only post results to the strand. `IClock`, `TimerService` and `ManualClock`/`ManualExecutor` make every state machine testable with no real time.
- **Operations.**
  - `start` validates synchronously. Bad input returns a Diagnostic and creates no op.
  - `Operation<T>` completes exactly once: CAS from Pending to Completing, terminal `Completed | Failed | Cancelled{reason} | TimedOut{phase}`. A cancel stores its reason before it signals.
  - `Progress` is coalesced to 10 Hz, has an optional total and is never NaN.
  - The `AwaitingUser{request_id}` phase has no deadline but can be cancelled.
  - Every `OpKind` has a mandatory deadline (table below) that is scaled by the runner multiplier: native x1, Wine x2, first run under Rosetta x4.
  - `OpRegistry` supports attach-by-id and keeps a terminal outcome until every handle is released, or for 10 minutes after Done.
  - `DisconnectPolicy` is `BoundToConnection` or `Detached`. Installs, play starts and host starts default to Detached.
- **User requests.** `UserRequestRegistry` is the only way the engine asks a human anything.
  - It emits `UserActionRequired{request_id, kind, payload, op_id?, session_id?}`.
  - `Requests.respond` resolves a request. The first valid answer wins by CAS, and later answers get `requests.already_resolved`.
  - Kinds: NeedsSecret, ConfirmJoin, NeedsJoinPassword, AutoServerConsent, ConfirmUnencryptedUpstream, AccountRenameConflict, RosettaInstall, AgentRequiresApproval, ChooseVersion, ConfirmUntested, ConfirmStopSessions.
- **Events.**
  - `EventEnvelope{kind, epoch, seq, session_id?, op_id?, payload}`. `seq` is global within an epoch.
  - Subscriptions have a filter and a bounded queue with coalescing keys: Progress per op, `*Changed` per collection, LogLine dropped with a counter, ViewDelta per view.
  - Op terminal events, session lifecycle events and `UserActionRequired` are never dropped. If one of them cannot be queued, the subscription is cleared and gets one `Resync`.
- **Logging.**
  - One process-wide `Logger`: an MPSC queue with a byte budget and one writer thread.
  - When the queue is full, game_output, wine, debug and trace drop first. Warn and above never drop.
  - The `Redactor` runs before every sink. It does exact matching on every registered `Secret`, token and session key, and applies the `-AUTH_PASSWORD=` key rule.
  - `logging` provides the files, rotation, `LogRing` and export.
- **Paths.**
  - `DataRoot` is `REBOOT_LAUNCHER_HOME`, otherwise the `IPlatformPaths` defaults: `%LOCALAPPDATA%\Reboot Launcher`, `~/Library/Application Support/Reboot Launcher` (plus Caches and Logs), or the XDG directories.
  - `root_hash16`.
  - `AppLayout` (§7.6) and `InstallLayout` (the bundled `reboot-backend`, `reboot-game-server`, msquic, bundled catalog and manifest, and the backend content directory).
- **GameVersion.** Strict parsing with the changelist kept separately. `canonical()` is at most 16 bytes. `version_bucket(GameVersion)` (`base/version_bucket.hpp`, apart so the sb registry header stays out of every TU) is byte-identical to sb `version_bucket`. It lives in base because catalog, builds, support, browser, gameserver, host and the game server all use it.

**OpKind deadlines.** Overridable, never infinite.

| OpKind | Deadline |
|---|---|
| HttpSmall | 20 s total |
| HttpDownload | 10 s connect; stall below 1 KiB/s for 30 s |
| Dns | 5 s |
| QuicConnect | 10 s |
| BackendReady | 30 s |
| ChildHello | 10 s |
| GameControlHello | 2 s x multiplier |
| HostReadiness | 120 s |
| GracefulStop | 5 s, then kill |
| UpnpDiscover / UpnpMap | 3 s / 5 s |
| Wmi | 5 s |
| EngineConnect | 10 s |
| ShutdownBudget | 30 s |

## 6. Ports (`core/ports`) and per-OS adapters

`PlatformServices` owns one instance of each port. `make_platform(PlatformOptions)` is defined by the linked OS module for the engine. `make_client_platform()` returns only the client-side ports (C) for `reboot_client`. The engine composition root takes `PlatformServices&`, and tests build the same composition from `core/testing` fakes.

| Port | Windows (`core-windows`) | macOS (`core-macos`) | Linux (`core-linux`) |
|---|---|---|---|
| IPlatformPaths | SHGetKnownFolderPath, exe dir, Velopack or portable | Application Support/Caches/Logs, bundle, translocation | XDG + getpwuid_r, `$APPIMAGE`, tarball |
| IFileSystem | ReplaceFileW + backup, FlushFileBuffers, LockFileEx, owner DACL, deny-write open | posix + F_FULLFSYNC | posix + directory fsync, OFD locks |
| IFileWatcher | ReadDirectoryChangesW | FSEvents | inotify |
| IDiskInfo | GetLogicalDriveStringsW, GetDriveTypeW, GetDiskFreeSpaceExW | statfs + volume keys | mountinfo + statvfs |
| ISecretStore | Credential Manager, DPAPI file fallback | Keychain (engine only; unavailable outside Aqua) | libsecret via dlopen with a deadline, 0600 file fallback; an erase that cannot reach the keyring leaves a tombstone file |
| IProcessLauncher (native children) | CreateProcessW + one Job, HANDLE_LIST, anonymous stdio pipes | posix_spawn + process group + kqueue NOTE_EXIT + stdin-EOF | pidfd spawn + PDEATHSIG + `systemd-run --user --scope` when available |
| ISessionHost (play) | Win32SessionHost over win32session (in-engine) | not provided; the engine builds `compat::WineSessionHost` | same as macOS |
| IRunnerPlatform (play) | not provided | CrossOver-source Wine + DXMT layout, quarantine strip, Rosetta | umu + GE-Proton, SLR setup, Kron4ek Wine fallback |
| IPortInspector | GetExtendedTcpTable/UdpTable v4/v6 | libproc PROC_PIDFDSOCKETINFO | sock_diag, /proc/net fallback |
| ILoopbackPeerInspector | not_supported | not_supported | sock_diag owner uid |
| IResolver | GetAddrInfoExW (cancellable) | getaddrinfo on a worker | getaddrinfo on a worker |
| IIpcListener (engine) / IIpcConnector (C) | named pipe with PipeTrust | AF_UNIX (posix) + getpeereid | AF_UNIX (posix) + SO_PEERCRED + LISTEN_FDS |
| IEngineStarter (C) | breakaway → on-demand task → CannotDetach | SMAppService.agent + `launchctl kickstart` → AgentRequiresApproval | `systemctl --user start` socket → `systemd-run --user` → setsid double fork (refused under a Steam reaper) |
| ICallerContextProbe (C) | session id, elevation, AllowSetForegroundWindow | Aqua session | display env allow-list |
| IShellLauncher | ShellExecuteExW, SHOpenFolderAndSelectItems, IFileOperation | NSWorkspace | portal OpenURI / xdg-open, gio trash |
| IIntegrationRegistrar | HKCU Classes `"<exe>" --activate-url "%1"`, HKCU Run, on-demand task | Info.plist scheme status, SMAppService agents | .desktop + xdg-mime, ~/.config/autostart, systemd user units |
| ISecurityProductProbe | WMI SecurityCenter2 on an MTA worker (5 s), Smart App Control state | returns nullopt | returns nullopt |
| IPrerequisiteProbe | Windows 10 1809+ | Apple Silicon, macOS 14+, Metal 3, Rosetta, firewall state, local network hint | python3 (umu), Vulkan loader, linger |
| ISystemInfo | RtlGetVersion, elevation, session id, Schannel trust | sysctl, Aqua session | os-release, Steam-reaper ancestry, display vars, CA path probe |
| IUpdateApplier | Velopack wait-exit-then-apply, restart `--resume` | Velopack bundle swap, execve `--resume` | versions/ + current/previous symlinks, execve shim `--resume` |
| IHttpTransport | core `net::CurlHttpTransport` (Schannel) | core curl (OpenSSL + SecTrust) | core curl (OpenSSL + CA probe) |
| IQuicTransport | core `net::MsQuicTransport` (app-local msquic) | core MsQuic (signed with the app) | core MsQuic |
| IRandom | foundation `OsRandom` | foundation | foundation |

`core/posix` holds the shared POSIX pieces used by macOS and Linux: the AF_UNIX listener and connector, flock and atomic replace, owner-only files, the posix_spawn base and signal setup. It is its own target and is linked only by `core-macos` and `core-linux`.

## 7. Engine

### 7.1 Composition and threading

`core/engine` builds every service with constructor injection from `PlatformServices`. All services live on the strand and take no locks. A service method is either:

- a synchronous query on the strand that returns `Result<T>`; or
- an op start that returns `Result<OpHandle>`, after which the work continues through posted continuations.

`ApiRouter` implements the generated `reboot.api.v1` handler interfaces (`api_dispatch`, §9) by calling services. It converts domain types and events to API messages. Services never see API types.

### 7.2 Lifecycle

Command line: `reboot-engine run [--foreground] [--origin=on-demand|service-manager] [--resume]`. Before it is parsed, a run that Velopack started for its install, update or uninstall hook (Windows) writes or removes the OS integration and exits. Startup order:

1. Resolve `DataRoot`.
2. Take `state/engine.lock`. If another engine holds it, exit 0; clients will reach the holder.
3. Start the logger.
4. Open the stores. A newer schema runs read-only; a failure falls back to in-memory. Either way the mode is reported in `HelloAck.storage_mode`.
5. Reap orphans from `runtime.json`.
6. Load components and refresh the catalog.
7. Reconcile integration, if `last_run_version` changed.
8. Create the IPC endpoint. This happens only while the lock is held, so a stale socket is unlinked only under the lock.
9. Start the services.
10. Apply the resume record.
11. Write `runtime.json`.

**Origins:**

- **OnDemand:** started by any client starter, including socket activation and kickstart.
- **ServiceManager:** started by the user's start-at-login entry (HKCU Run, the login agent, or an enabled systemd user service), which passes `--origin=service-manager`.
- **Foreground:** `run --foreground`.

**Idle exit:** only OnDemand, 5 minutes after the last client connection, session, backend lease or pin, publication and Detached op are gone.

**`ShutdownCoordinator`** runs ordered steps, each with its own budget, inside a 30 s total. It never waits on a client.

1. Refuse new sessions and ops.
2. Unpublish.
3. Delete port mappings.
4. Drain or stop hosts, following `host.update_policy` and the reason.
5. Stop play sessions: Shutdown, a 5 s grace, then kill.
6. Stop the backend.
7. Close the front and the game channel.
8. Flush the stores.
9. Flush the logs.

`Drain{Update, UserStop, Replace}` and `Shutdown{Now, WhenIdle}` are bootstrap calls.

**OS signals and logoff.** SIGTERM, SIGINT, a closed console and a Windows logoff or shutdown run the same steps inside 4 s, with shorter step budgets. A windowless Windows engine learns of a logoff from a hidden top-level window: Windows ends a process with no visible window 5 s into an unanswered `WM_ENDSESSION` and does not let it cancel the end, so the window holds `WM_ENDSESSION` for at most 4.5 s (`ports::kSessionEndHold`), which covers the 250 ms signal poll and that 4 s shutdown.

### 7.3 Sessions and leases

- A session is `{id, kind Play|Host, phase, lease, pinned_inputs, parent?}`. `pinned_inputs` are the settings snapshot, build, payload version, runtime id and game-server sha256. A setting edited later never changes a running session.
- `Lease` is `Engine` (the default; the session survives every UI) or `Client(connection)` (the session stops when that connection closes).
- **Play** needs the caller and the engine in the same OS session: the Windows session id, the Aqua session, or a display on Linux. Otherwise it returns `play.wrong_session`. Host, stop, list and status work from any OS session.

### 7.4 Child supervision and liveness

`process::ChildSupervisor` spawns through `IProcessLauncher`, runs the framed stdio channel and correlates requests and replies. It also:

- sends `Ping` every 5 s and treats three missed `Pong`s as a hang;
- applies a per-role reaction: the backend restarts with backoff (1 s to 30 s, at most 5 restarts in 5 minutes, then Failed); a game server is reported and then stopped by host policy; the game process (through winhost or our DLL) is only reported, and not during Loading;
- records children in `runtime.json`.

### 7.5 Updates

`updates` is the only updater.

1. It checks the signed manifest and stages the app through `IUpdateApplier`.
2. It applies only when nothing is live, or after a consented `Drain{Update}`. Hosts follow `host.update_policy`, which defaults to `after_match` and sends the game server `Drain`.
3. It writes `state/resume.json` and the pending marker (with attempts and rollback), then applies and restarts.
4. The next start reconciles integration and confirms the marker after an IPC self-test.

`min_supported` blocks only new sessions. Payload and runtime updates apply to new sessions only, and version N-1 is kept until a session succeeds on the new one. Clients that find `state/update-in-progress` wait and retry instead of starting an engine.

### 7.6 Stores (`AppLayout`)

| File | Contents |
|---|---|
| `config/settings.json` | Settings registry document |
| `config/frontend/<shell>.json` | Opaque UI blob |
| `data/library.json` | Builds and per-role selection |
| `data/accounts.json` | The one player name and one account record (record id, role, tag) per role; each account id is the name plus its tag |
| `data/host-profiles.json` | Host profiles |
| `state/state.json` | Onboarding, notices, TLS memory, integration declined flags, last_run_version |
| `state/host-identity/<profile>.json` | Owner-only |
| `state/runtime.json`, `state/resume.json`, `state/update-in-progress`, `state/engine.lock`, `state/spawn.lock` | Engine runtime state |
| `data/components/`, `data/prefixes/`, `data/backend/`, `data/game-server/<session>/` | Component, prefix, backend and per-host-session data |
| `cache/catalog.json`, `cache/manifest.json`, `cache/game-server-describe.json` | Caches |
| `logs/` | Log files |

## 8. Engine IPC (private, `core/ipc` + `core/contracts/ipc.hpp`)

**Endpoint.**

- Windows: `\\.\pipe\reboot-engine-<UserSID>-<hash16>`. It has an explicit DACL (the user and SYSTEM), a medium integrity label, FILE_FLAG_FIRST_PIPE_INSTANCE and PIPE_REJECT_REMOTE_CLIENTS. The engine checks the client's token SID. The client opens the pipe with SECURITY_SQOS_PRESENT|SECURITY_IDENTIFICATION and checks the pipe owner SID and the server process's token user before it writes anything.
- macOS: `confstr(_CS_DARWIN_USER_TEMP_DIR)/reboot-launcher/<hash16>.sock`, never under /tmp. The path is checked against the 104-byte sun_path limit.
- Linux: `$XDG_RUNTIME_DIR/reboot-launcher/<hash16>.sock`. The fallback is `/tmp/reboot-launcher-<uid>/`, after an lstat check that the owner matches and the mode is 0700. The engine also accepts an inherited `LISTEN_FDS` socket.
- POSIX: the socket is 0600 in a 0700 directory, and both ends check the peer uid.
- A failed check returns `RB_E_ENGINE_ENDPOINT_UNTRUSTED`.

**Framing.** sb StreamFramer with `kIpcFrameCap`. The frame type selects the message.

| Direction | Messages |
|---|---|
| client to engine | `Hello{client_kind, client_build, abi_version, pid, caller_context{os_session, elevated, display_env[]}}`, `Call{req_id, method_id, payload, timeout_ms}`, `Start{req_id, method_id, payload, detached}`, `Cancel{op_id}`, `Attach{op_id}`, `Release{op_id}`, `Subscribe{sub_id, filter}`, `Unsubscribe{sub_id}`, `Credit{sub_id, n}`, `SecretPut{req_id, target, bytes}`, `SecretReveal{req_id, target}`, `LogWrite{level, text}`, `Ping`, `Goodbye` |
| engine to client | `HelloAck{engine_build, epoch, pid, image_path, canonical_root, origin, storage_mode, secrets_available, compatibility Full|BootstrapOnly}`, `Reply{req_id, payload | WireDiagnostic}`, `Started{req_id, op_id}`, `OpResult{op_id, outcome}`, `EventBatch{sub_id, events[]}`, `Resync{sub_id}`, `ForegroundHint{pid}`, `Pong`, `Goodbye{reason}` |

**Compatibility.**

- The same engine build gets `Full`.
- A different build gets `BootstrapOnly`. That frozen subset never changes layout: Hello/HelloAck, `Engine.status`, `Engine.drain`, `Engine.shutdown`, `Engine.restart_when_idle`, `Sessions.list` and `Sessions.stop`. Every other call returns `RB_E_ENGINE_VERSION_MISMATCH`. This subset is the only promise that holds across releases (update and drain flows need it).
- The epoch tells a reconnecting client whether its ops and sessions can still be attached (same epoch) or are gone (new epoch).

**Per-connection limits.** At most 64 subscriptions, 256 outstanding calls and a 32 MiB outbound budget. A client over budget gets `Resync` on its lossy subscriptions. A client that hits the budget twice within 10 s is disconnected with `Goodbye{SlowConsumer}`.

**Secrets** arrive only in `SecretPut`. The engine wipes the frame buffer after use and answers with a `Reply` once the secret is stored, or with why not. `SecretReveal` is allowed only for the host join password.

## 9. Public API, C ABI and SDK

### 9.0 Compatibility before v1

No API or ABI compatibility is promised until v1. Every first-party client ships in the same package as the engine and its `reboot_client`, and engine IPC already requires an exact build match, so the C ABI and the schema are promised only **within one release**, plus the frozen bootstrap subset (§8).

- `RB_SCHEMA_FINGERPRINT`, `RB_ABI_MAJOR.MINOR` and `buf breaking` stay built and checked, but nothing beyond the bootstrap subset is promised across releases before v1.
- Clients need no unknown-case tolerance beyond what decoding already does: `api.unknown_case` and `api.conflicting_cases` decode errors, and events of a kind the build does not know are skipped.
- At v1 the C ABI and schema are frozen only for a named external consumer. Otherwise the CLI's `--json` output (§9.4) is the external automation contract.

### 9.1 `reboot.api.v1` (`core/api`, schema in `launcher/schema`)

The schema lives in `launcher/schema/proto/reboot/api/v1/*.proto`: one file per service, plus `common`, `events` and `options`. It uses the sb-codec subset, and the generator enforces it:

- field numbers are contiguous and in declaration order (field number = declaration index + 1);
- fields are never removed, only deprecated;
- there are no maps, nested types, recursive messages or streaming methods; fields are bool, uint32/64, sint32/64, string, bytes, enum or message;
- every enum starts at 0, and its values carry the enum's name as prefix;
- a `oneof` declares its members consecutively. The C++ gets consecutive `std::optional` members. Decoding requires exactly one to be set: more than one is `api.conflicting_cases`, and none is `api.unknown_case`, a case from a newer schema.

`options.proto` carries the metadata:

- `(service_info) = {id}` gives the high 16 bits of every `method_id` in the service;
- `(method_info) = {id, kind, progress, disconnect}` gives the low 16 bits (never reused), CALL or OPERATION, the progress unit (none, item count or byte count) and the default disconnect policy (bound or detached). A CALL has neither progress nor a policy;
- `(fixed_size) = N` makes a bytes field exactly N bytes; 16 maps to `rb::Uuid`.

**Generator.** `core/api/tools/protoc-gen-reboot-cpp` is a Rust crate built on prost-reflect. It runs as a protoc plugin through `buf generate` (`schema/buf.gen.yaml`, one invocation over every file), or standalone with `--protoc --proto-root --out [--check]`. It writes, relative to `launcher/core`:

- `api/include/reboot/api/v1/<file>.hpp`: every aggregate and enum of one `.proto` (the one exception to one main type per header), header-only and with no registry;
- `api/include/reboot/api/v1/method_table.hpp`: the `MethodTable` and the `k<Service><Method>` ids;
- `api_dispatch/include/reboot/api_dispatch/v1/handlers.hpp` (one handler interface per service), `api_dispatch/include/reboot/api_dispatch/v1/dispatch.hpp` and `api_dispatch/src/dispatch.cpp`;
- `client/include/reboot/client_methods.h`: the `RB_METHOD_<SERVICE>_<METHOD>` ids and `RB_SCHEMA_FINGERPRINT`, a 64-bit FNV-1a hash over every enum, message, field and method;
- `sdk/include/reboot/sdk/methods.hpp` (one typed member per method for `rb::sdk::Context`) and `sdk/include/reboot/sdk/event_data.hpp` (the `EventData` variant) (§9.3).

`DecodeError` is plain data in `reboot/api`; `api_dispatch` turns it into a `Diagnostic` with the `api.*` messages.

The output is committed. The `schema` CI job runs `core/api/tools/check_schema.py`:

- the generator in `--check` mode;
- the golden vectors (`make_golden.py`, against protoc's encoding of `tests/data/*.txtpb`);
- `buf lint` (STANDARD, less `ENUM_ZERO_VALUE_SUFFIX` and `SERVICE_SUFFIX`);
- `buf breaking` (FILE) against the newest release tag that contains the schema. Before v1 it guards nothing that is promised (§9.0); it is kept so the gate exists at v1.

A schema change changes `RB_SCHEMA_FINGERPRINT`. `RB_ABI_MAJOR.MINOR` versions only `client.h`.

| Service | Calls | Operations (default policy) | Events |
|---|---|---|---|
| Engine | status, info, drain, shutdown, restart_when_idle, operations | - | EngineState, OpStarted, op progress and completion |
| Settings | snapshot, patch, reset, search, frontend_state_get/put, languages, descriptors, console_keys, console_key_from_native | - | SettingsChanged, StorageModeChanged |
| Library | list, get, select, update | relocate, import, remove (bound) | LibraryChanged |
| Catalog | list | refresh (bound) | CatalogChanged |
| Install | - | suggest_destination, discard_staging, delete_unregistered (bound); install (detached) | progress |
| Support | query | - | - |
| Components | list | ensure, runtime_setup (detached); remove (bound) | ComponentChanged |
| Identity | get, set_display_name, reset | - | IdentityChanged |
| Secrets | state, clear | - | SecretStateChanged |
| Play | plan | start (detached) → SessionId | via Sessions |
| Host | profiles_list/create/update/delete, share_link, status, cancel_match_end | start (detached); command, identity_export, identity_import (bound) | HostPhaseChanged, HostListening, ReachabilityChanged, PortMappingChanged, PublishStateChanged, MatchEvent, PlayerEvent, HostProfilesChanged |
| Sessions | list, get, set_lease | stop (bound) | SessionStateChanged, SessionSpawned, SessionDegraded, SessionEnded |
| Backend | status, data_dir, set_target, accounts_list | start, stop, accounts_reset/delete/prune/rename (bound) | BackendStateChanged, BackendAccountsChanged, XmppUnavailable |
| Browser | open_view, update_view, close_view, state, buckets | resolve, search (bound) | BrowserConnectionChanged, ViewSnapshot, ViewDelta |
| Join | parse_address, target, set_custom_target, clear_target | resolve_link, grant (bound) | JoinTargetChanged |
| Updates | status | check (bound); apply (detached) | UpdateAvailable, UpdateStaged, EngineUpdating, UpdateFailed |
| Integration | status, prerequisites, shell_open_url/open_path/reveal | apply, remove, remediate, purge (bound) | IntegrationChanged, PrerequisitesChanged |
| Guidance | notices_list/dismiss, onboarding_state/advance/skip, links | - | NoticeAdded, NoticeRemoved, OnboardingChanged |
| Logs | read | export (bound) | LogLine |
| Requests | pending, respond | - | UserActionRequired, UserActionResolved |

`EventPayload` is a single oneof, and each member has an `EventKind` value of the same name. `EventKind` also holds Resync, ConnectionLost and Reconnected, which only the client library raises.

### 9.2 `reboot/client.h` (`core/client`)

`reboot_client` is the one shared library and the binary boundary of every client. It exists for toolchain and CRT isolation: it is built with the core's toolchain (`/MT`, C++26, vcpkg Boost), frees its own buffers, and exposes only C, so a `/MD` MSBuild app, Swift and a prebuilt Linux `.so` consumer can all load it. Every export is `extern "C"` and `noexcept` (`RB_NOEXCEPT`). `RB_API` is dllexport/dllimport on Windows and default visibility elsewhere. Any function may be called from any thread concurrently, except `rb_ctx_destroy`, which must be the last call on its context. `client.h` includes the generated `client_methods.h`.

```c
#define RB_ABI_MAJOR 1
#define RB_ABI_MINOR 0
typedef int32_t rb_status;                 /* not an enum, so a later minor can add codes */
typedef struct rb_buffer { const uint8_t* data; size_t size; void* internal; } rb_buffer;

uint32_t    rb_abi_version(void);                                   /* (major << 16) | minor */
const char* rb_status_name(rb_status);                              /* static; "RB_UNKNOWN" for an unknown code */
rb_status rb_ctx_create(const rb_ctx_options* options, rb_ctx** out);
void      rb_ctx_destroy(rb_ctx*);
rb_status rb_call (rb_ctx*, uint32_t method, const uint8_t* request, size_t request_size, uint32_t timeout_ms, rb_buffer* response);
rb_status rb_start(rb_ctx*, uint32_t method, const uint8_t* request, size_t request_size, uint32_t flags, uint64_t* op_id);
rb_status rb_op_attach (rb_ctx*, uint64_t op_id);
rb_status rb_op_cancel (rb_ctx*, uint64_t op_id);                  /* idempotent */
rb_status rb_op_result (rb_ctx*, uint64_t op_id, rb_buffer* outcome); /* RB_PENDING until terminal */
rb_status rb_op_release(rb_ctx*, uint64_t op_id);
rb_status rb_subscribe  (rb_ctx*, const uint8_t* filter, size_t filter_size, uint64_t* sub);
rb_status rb_unsubscribe(rb_ctx*, uint64_t sub);
rb_status rb_events_next(rb_ctx*, uint64_t sub, uint32_t timeout_ms, rb_buffer* event); /* 0 polls; RB_WAIT_FOREVER */
void      rb_events_set_wake(rb_ctx*, uint64_t sub, void (*wake)(uintptr_t user), uintptr_t user);
rb_status rb_secret_put   (rb_ctx*, const uint8_t* target, size_t target_size, const uint8_t* secret, size_t secret_size);
rb_status rb_secret_reveal(rb_ctx*, const uint8_t* target, size_t target_size, rb_buffer* out); /* host join password only */
void      rb_log_write(rb_ctx*, int32_t level, const char* utf8, size_t size);       /* RB_LOG_TRACE..RB_LOG_ERROR */
rb_status rb_last_error(rb_buffer* diagnostic);                     /* this thread's last call */
void      rb_buffer_release(rb_buffer*);                            /* wipes, frees, zeroes */
```

Engine details (build, pid, image path, data root and schema fingerprint) come from the `RB_METHOD_ENGINE_STATUS` and `RB_METHOD_ENGINE_INFO` calls. There is no separate HelloAck accessor, no batch drain and no waitable handle.

**`rb_ctx_options`.** Fields are only appended, and a newer caller's extra fields must be zero for an older library.

- `struct_size`;
- `data_root`: UTF-8 and absolute. NULL means `REBOOT_LAUNCHER_HOME`, then the platform default;
- `client_kind`: `RB_CLIENT_UNKNOWN`, `_WINDOWS_GUI`, `_MAC_GUI`, `_LINUX_GUI`, `_CLI` or `_TEST`;
- `launch_mode`: `RB_LAUNCH_AUTOSTART` or `RB_LAUNCH_CONNECT_ONLY`;
- `connect_deadline_ms`: 0 means 10 000.

**Flags:** `RB_START_DETACHED` and `RB_START_BOUND`. Both at once is `RB_E_INVALID_ARG`. Without either, the method's default applies.

**Status codes.** `rb_last_error` names the Diagnostic behind every code below `RB_OK`.

- general: `RB_OK`, `RB_PENDING`, `RB_E_TIMEOUT`, `RB_E_INVALID_ARG`, `RB_E_REMOTE` (the engine's Diagnostic), `RB_E_INTERNAL`, `RB_E_LIMIT` (too many calls or subscriptions);
- engine (from -100): `RB_E_ENGINE_UNAVAILABLE`, `RB_E_ENGINE_ENDPOINT_UNTRUSTED`, `RB_E_ENGINE_CANNOT_DETACH`, `RB_E_ENGINE_VERSION_MISMATCH`, `RB_E_ENGINE_OTHER_SESSION` (`rb_start` only), `RB_E_ENGINE_ROOT_MISMATCH`;
- autostart (from -200): `RB_E_ELEVATED_AUTOSTART_REFUSED`, `RB_E_NO_INTERACTIVE_SESSION`, `RB_E_AGENT_REQUIRES_APPROVAL`;
- connection and version (from -300): `RB_E_CONNECTION_LOST`, `RB_E_ABI_MISMATCH`, `RB_E_CLOSED`.

**Semantics:**

- **Memory.** Inputs are borrowed for the duration of the call. Outputs are `rb_buffer`s owned by the library and freed only through `rb_buffer_release`, which wipes them. An output buffer must be zeroed or released on entry; one that still holds data is `RB_E_INVALID_ARG`.
- **Errors.** `rb_last_error` is per thread. It returns the Diagnostic of that thread's last failed call, or the warning of a successful `rb_ctx_create` (`ipc.engine_image_differs`). `RB_E_REMOTE` from `rb_call` puts the Diagnostic in `response`.
- **Payloads.** The library never decodes API payloads. It reads only the envelope fields and the few `EventKind` values it raises or follows.
- **Calls and ops.**
  - `timeout_ms` 0 sends no deadline, so the engine applies the method's default.
  - `rb_start` refuses bad input synchronously and creates no op. On `RB_OK` the op is attached to the context.
  - `RB_METHOD_ENGINE_OPERATIONS` and the OpStarted event let another UI find a live op and `rb_op_attach` it.
- **Wake callback.** It carries no event and runs on a library thread with no lock held that other functions take. It fires at most once until the queue has been popped empty, and must not block. `rb_unsubscribe` and `rb_ctx_destroy` wait for a running callback, so neither may be called from one.
- **Exactly-once.** For an op attached to the context, a subscription whose filter admits its OpCompleted and names no session receives that OpCompleted once, across Resync and reconnects. `rb_op_result` always returns the same stored Outcome.
- **Connection loss.**
  - Pending calls fail with `ipc.connection_lost`, and every subscription gets ConnectionLost.
  - Only Autostart mode reconnects. It then delivers `Reconnected{epoch}`, resubscribes and delivers Resync. In the same epoch, pending ops are attached again and an outcome that arrived meanwhile is replayed. In a new epoch, they fail.
  - In ConnectOnly mode, or when reconnecting fails for good, pending ops fail and the subscriptions close (`RB_E_CLOSED` once drained). An engine of another build closes them too.
- **Autostart** runs `IEngineStarter`, which serialises on `state/spawn.lock`, so racing clients start only one engine. It is refused from an elevated or non-interactive client.
- **Windows foreground.** The library calls `AllowSetForegroundWindow` for the engine pid after each handshake, and for the pid in each `ForegroundHint`.
- **Caller context.** It is captured once at `rb_ctx_create` and sent in Hello.
- **Secrets.** `rb_secret_put` sends the secret in its own `SecretPut` frame, wipes the library's copy once it is written, and returns once the engine answers: `RB_E_REMOTE` when the secret was refused or not saved. The engine redacts `rb_log_write` lines.

**Swift** (the macOS client) uses `client.h` directly through a module map, with SwiftProtobuf generated from the same `.proto` for the payloads; the golden vectors prove protoc compatibility. It checks the ABI major and the schema fingerprint as the SDK does.

### 9.3 `rb::sdk` (`core/sdk`)

The SDK is the interface every C++ client uses: the WinUI 3 C++/WinRT app, the CLI and, later, the Linux client. To UI code the core is a C++ API; the process boundary shows up only as async calls and connection state.

- **Header-only, compiled by each consumer** with its own toolchain and CRT, as C++23. It depends only on `reboot/client.h`, the header-only api types (`core/api`), `core/base` and the header-only sb codec with Boost.PFR. It links no core static library, no Asio and no message registry, and nothing of the core's crosses the DLL boundary except C. `reboot/sdk/sdk.hpp` includes all of it.
- **Generated:** `methods.hpp` (a CRTP `Methods<Self>` mixin with one typed member per method) and `event_data.hpp` (the `EventData` variant) from protoc-gen-reboot-cpp; `english_catalog.hpp` (every registered message's English template) from `reboot-l10n render`, which the `l10n_catalog_complete` gate keeps current.
- **Shape:**

| Type | What it does |
|---|---|
| `Context::connect(ConnectOptions)` → `Expected<Context>` | RAII over `rb_ctx`, shared by copies, ops and subscriptions. Options: data root, client kind, launch mode, deadline, the ABI and schema fingerprint the client was built for, and whether a bootstrap-only engine is acceptable. It refuses another ABI major (`sdk.abi_mismatch`) and another schema (`sdk.schema_mismatch`, against `Engine.info`), and keeps the connect warning |
| `ctx.<service>_<method>(request[, timeout])` → `Expected<Response>` | a CALL; `ctx.call<Response>(id, request)` underneath |
| `ctx.<service>_<method>(request[, Disconnect])` → `Expected<Op<Response>>` | an OPERATION; `attach_<service>_<method>(op_id)` attaches one started elsewhere |
| `Expected<T>` = `std::expected<T, Error>` | `Error` holds the `rb_status`, the Diagnostic's id, args, detail, kind, retryable flag and OS code, and `english()`, the English template rendered with the args |
| `Op<T>` | `cancel()`, `poll()`, `wait(timeout, on_progress)`, `observe(event)`, `progress()`; the outcome is `Outcome<T>`: the response, an `Error`, `Cancelled{reason}` or `TimedOut{phase}`, or `into_expected()` |
| `Subscription` | `next(timeout)` to pull, or `deliver(dispatcher, handler)` to have the wake callback push events through a `Dispatcher`; events this build cannot read are skipped and counted |
| `Event` | the envelope plus `data`, an `EventData` variant over every payload type, the client-raised `Resync`, `ConnectionLost` and `Reconnected`, and the SDK's `Closed` |
| `Dispatcher` | one `post()`; a UI adapts its loop (WinUI `DispatcherQueue::TryEnqueue`, GLib `g_idle_add`, Qt `QMetaObject::invokeMethod`, FTXUI `Post`). `QueueDispatcher` (drained by the caller, for a blocking CLI) and `ThreadDispatcher` (its own thread, for callbacks) ship with the SDK |
| `LiveList<T>` | a snapshot call, then deltas; a Resync or Reconnected takes a new snapshot and a ConnectionLost marks it stale. `sessions_source()` keeps `Sessions.list` current |
| `put_secret` / `reveal_secret` | secrets travel as `SecretString`, wiped when dropped; reboot_client wipes its buffers |
| `connection()` | `Connected`, `Reconnecting` or `Closed`, followed from the subscriptions' events |

```cpp
rb::sdk::Expected<rb::sdk::Context> ctx = rb::sdk::Context::connect({.client_kind = rb::sdk::ClientKind::Cli});
if (!ctx) return fail(ctx.error().english());
auto profiles = ctx->host_profiles_list();                        // Expected<api::HostProfilesListResponse>
auto op = ctx->host_start({.profile = profiles->profiles[0].id}); // Expected<Op<api::HostStartResponse>>
auto outcome = op->wait(std::chrono::minutes{2}, [](const rb::api::Progress& p) { show(p.phase, p.done); });

rb::sdk::QueueDispatcher ui;                                      // or an adapter over the UI loop
auto sessions = rb::sdk::LiveList<rb::api::SessionSummary>::open(*ctx, ui, rb::sdk::sessions_source(), redraw);
```

- **No extra tolerance.** Unknown oneof cases and conflicting cases fail decoding; events of unknown kinds are skipped (§9.0).
- **Closure check.** `cmake --install <build> --component sdk` lays out the SDK headers, the api types, base, `client.h`, the sb codec headers, Boost.PFR and `reboot_client`. CI builds `core/sdk/consumer` against only that: with the dynamic CRT (`/MD`) on Windows and with the system toolchain on Linux and macOS.

### 9.4 CLI

The CLI (`launcher/cli/`) is C++26 with CLI11 and FTXUI over `rb::sdk`. Its `--json` output has stable, documented keys and is the scripting and third-party automation surface (bots, panels, tournament scripts).

## 10. Child-process contracts (`core/contracts`)

### 10.1 Common rules

- **Framing.** `varint type | varint length | payload`, with sb codec aggregates.
  - The frame type selects the message. Type ranges: common 0x10-0x1F, ipc 0x100-0x1FF, backend 0x200-0x2FF, game_server 0x300-0x3FF, game_client 0x400-0x4FF, winhost 0x500-0x5FF.
  - Messages carry no floating-point fields.
- **Handshake.** The child sends `Hello` first, and the engine answers `Welcome`. The `protocol` integer must match exactly; otherwise the error is `child.protocol_mismatch`, or `payload.abi_mismatch` for game-control.
- **Requests and replies.** Requests may flow in both directions and carry `req_id`. Every request gets exactly one `CommandResult{req_id, ok, WireDiagnostic?}`, one `Unsupported{req_id}`, or a typed reply.
- **Carriers.**
  - Native children (backend, game server) use stdio pipes. Stdin EOF means stop. stderr goes through `LineReader` into the session log and is never treated as fatal.
  - In-game and in-prefix peers (our client DLL, winhost) use loopback TCP with a 32-byte CSPRNG token, compared in constant time. Wine cannot reach a host pipe or AF_UNIX socket.
- **Secrets** travel only in frames, on a native child's stdin, in winhost's token file, or in the game's Windows command line and environment block, never in a Unix argv or environment. Under Wine the game's Unix argv comes from the command line CreateProcessW gets, and its Unix environment is winhost's, so winhost creates the game with its padded exe alone and writes the full command line into the suspended game before it runs (§10.5).
- **winhost's token file** is the one way winhost gets its token, under every Wine runner: pressure-vessel gives its container `/dev/null` as stdin, and the game would inherit anything in winhost's environment. `IRunnerPlatform::write_token_file` (core/posix `TokenFiles`) writes `<base>/ctl-XXXXXX/token`: `base` is a real 0700 directory of the effective uid's, created if missing and refused (`posix.not_a_directory`, `posix.directory_not_private`) otherwise; `ctl-XXXXXX` comes from `mkdtemp` (0700); the file is opened `O_CREAT|O_EXCL|O_NOFOLLOW` with mode 0600 whatever the umask, and holds the 43-character token and nothing else. `base` is `$XDG_RUNTIME_DIR/reboot` on Linux when `$XDG_RUNTIME_DIR` is absolute and a real 0700 directory of the user's (a tmpfs that never reaches the disk), else `<data root>/winhost-tokens`; on macOS it is the per-user temp dir (`confstr(_CS_DARWIN_USER_TEMP_DIR)`) plus `/reboot`, with the same data-root fallback. pressure-vessel does not share `$XDG_RUNTIME_DIR`, so under Umu `WineSessionHost` adds the session's `ctl-XXXXXX` directory alone, read-only, to `PRESSURE_VESSEL_FILESYSTEMS_RO`; plain Wine and the macOS runtime need no sharing. Only the path, mapped through `PathMapper`, reaches winhost, as its only argument, which the game never inherits. The engine removes the file (zeroed first) and its directory as soon as winhost's `Hello` is accepted, before the game exists, and on every other way the session ends: runner exit, stop, a dropped session or a failed launch. `WineSessionHost` sweeps every `ctl-*` entry an earlier engine left under `base` when it is built.
- **Liveness.** Ping and Pong, as in §7.4.
- **Shared messages:** `Ping`, `Pong`, `CommandResult`, `Unsupported`, `Log{level, category, text}`, `WireDiagnostic{id, args[{name, kind, value}], detail?, os_origin, os_code?, retryable}`.

### 10.2 Backend (implemented in phase 2): `contracts/backend.hpp`

**Spawn.** `<install>/reboot-backend --control=stdio`, with cwd `data/backend`.

- The environment is the EnvBuilder base plus `REBOOT_BACKEND_DATA=<root>/data/backend` and `REBOOT_BACKEND_CONTENT=<install>/backend-content`, which is read-only.
- It runs in its own Job or process group.

**Handshake.**

- `BackendHello{protocol, build, content{schema, serial}}`
- `BackendWelcome{bind_address (127.0.0.1, or 0.0.0.0 when backend.allow_lan), data_root, log_level}`
- then the event `Ready{http_port}`. HTTP and WebSocket share that one OS-assigned listener.

**Engine → backend requests:**

| Request | Reply / purpose |
|---|---|
| `RegisterAccount{account_id, record_id, role}` | Replayed after every restart |
| `RenameAccount{old, new, on_conflict}` | |
| `MintLaunchCredential{account_id, kind ExchangeCode\|LaunchSecret, build_cl}` | Returns `{value (Secret), expires_at}`. Single use, 5 minutes, bound to the build |
| `ConfigureSession{session_key, account_id, origin, console_key, build{version, cl}}` / `EndSession{session_key}` | Live sessions are replayed after a restart |
| `Accounts{List\|Reset\|Delete\|Prune{older_than, kind}}` | |
| `PurgeData` | |
| `ContentInfo`, `Health` | Health returns `{version, content, http_port}` |
| `Drain` | |

**Backend → engine:**

- the request `ResolveMatchTarget{account_id, playlist}`, which returns `{endpoint?, beacon_port?}`;
- the events `LoginObserved{account_id, session_key}` (a fallback LoggedIn signal), `AccountRenameConflict`, `Log`.

**Obligations:**

- bind exactly what Welcome says, and exit non-zero on a bind failure;
- keep mutable state only under `REBOOT_BACKEND_DATA`;
- accept the `X-Reboot-Session`, `X-Reboot-Xmpp` and `X-Reboot-Console-Key` headers only from the front;
- never accept a fixed "Rebooted" password;
- answer `GET /reboot/v1/backend-info`.

**Stop.** Close stdin, allow a 5 s grace, then kill.

### 10.3 Game server (implemented in phase 3): `contracts/game_server.hpp`

`reboot-game-server` is a standalone C++26 program. It does not run the Fortnite binary; with the Unreal Engine source available, it implements what hosting needs itself.

**Describe.** `reboot-game-server --describe` writes one `GameServerDescription` frame to stdout and exits:

- `protocol`, `build`;
- `supports[]{version_min, version_max, cl_ranges[]}`;
- `sockets[]{role Game|Beacon}`;
- `capabilities{in_process_reset, needs_backend, operator_commands[]}`.

The engine caches the description by the binary's sha256 in `cache/game-server-describe.json`. The cache sizes the port block before spawn and feeds the SupportPolicy host cells, without running a server.

**Spawn.** `--control=stdio`, cwd and logs in `data/game-server/<session>/`, the EnvBuilder base environment, one process per host session, in its own Job or process group.

**Handshake.**

- `ServerHello` repeats the description, and it must equal the cached copy.
- `ServerWelcome{ServerConfig}`, where `ServerConfig` is:
  - `session_id`;
  - `game{version, cl, build_root?}`;
  - `listen{bind_address (default 0.0.0.0, IPv4), ports[] (one per declared socket, in order)}`;
  - `backend?{origin, account_id, service_token}`;
  - `match{playlist, start_policy AutoAtPlayers(n)|Manual, max_players, tick_rate}`;
  - `operators{ip_cidrs[]}`, `bans[]`;
  - `log_dir`.

**Events:**

- `Listening{bound[]{role, port}}`, or `ListenFailed{port, os_error, stage}`;
- `StateChanged{Lobby|Warmup|InProgress|Ending}`;
- `PlayerJoined{account_id (unverified), display_name, address}` and `PlayerLeft{…}`;
- `PlayerCount{n}`;
- `MatchEnded{reason, winner?, placements[]}`;
- `Fatal{code, detail}`;
- `Log`.

**Engine → server requests:** `StartMatch{countdown}`, `EndMatch`, `Reset`, `Kick{player}`, `SetBans{bans[]}`, `SetOperators{ip_cidrs[]}`, `RunCommand{text}`, `Drain`, `Shutdown{grace_ms}`.

**Obligations:**

- bind exactly the given ports, with no next-port fallback;
- report what was bound;
- keep the block across `Reset`;
- answer the 25-byte rbsb/1 probe (0x01, 23 x 0x00, 0x04) on the game port;
- refuse a duplicate UniqueNetId unless the existing connection is from the same IP or has been silent for 5 s;
- trust operator commands only from the engine channel, plus remote operators on the IP allowlist;
- never show a window or a MessageBox;
- never block on writing stdout.

### 10.4 In-game client DLL (engine side now; DLL in `payload/`): `contracts/game_client.hpp`

**Bootstrap environment.** It goes in the game's Windows environment block, which winhost builds under Wine:

- `REBOOT_CTL=tcp://127.0.0.1:<port>`
- `REBOOT_CTL_TOKEN`
- `REBOOT_SESSION`
- `REBOOT_ROLE=client`

The DLL erases them after reading.

**Connection.**

- Preamble: `"RBCTL\0"` + u16 `payload_abi`.
- `Hello{token, role ClientDll, dll_build, protocol, game{version, cl}, exe_sha256, pid}` must arrive within 2 s x the multiplier.
- `Welcome{ClientDllConfig}`:
  - `session_id`;
  - `origin` (the front URL with `/s/<key>/`);
  - `sentinels[]`, `host_suffixes[]`, `ws_rewrite`;
  - `features{auth_redirect, console, memory_fix, exit_suppression}`;
  - `console_key`, `test_mode`, `log_level`.

**Events:**

- `Loaded{dll_version, table_version, build_match}`
- `PatchResult{id, status, rva}`
- `RedirectReady`
- `HookFailed{step, required}`
- `LoggedIn`
- `WindowCreated{hwnd}`
- `ExitRequested{kind, code, phase}`
- `ConsoleReady`
- `Fatal{step}`
- `Log`

**Requests:** `Shutdown`, plus `Join{address}` and `Quit` in test mode only.

**Lifetime.** A disconnect after Welcome ends the session. A duplicate (session, role, module) is rejected.

### 10.5 winhost (`payload/winhost`): `contracts/winhost.hpp`

- winhost takes `REBOOT_CTL` from its environment and reads its token once from the token file its only argument names (§10.1, a Windows path such as `Z:\run\user\1000\reboot\ctl-XXXXXX\token`); the file must hold exactly the 43-character token, and every buffer it passed through is wiped. No token sits in a Unix environment or argv the game inherits. It connects to the game-channel listener with `Hello{token, role Winhost, build, protocol, pid}`, and the engine removes the file before it answers.
- winhost creates the game suspended with its quoted exe alone, padded with spaces to the full length, and writes the full command line into the game's process parameters before anything in it runs; core-linux/runner's Wine gate checks `/proc/<game pid>/cmdline` and `environ` for `-AUTH_PASSWORD` and `REBOOT_CTL_TOKEN`.
- `Welcome{SpawnGame{exe, argv (UTF-16), env_block (UTF-16), cwd, companions[], inject[]{path, sha256, strategy EarlyBirdApc|AfterResume, phase Early|LoggedIn}}}`.
- Events: `Spawned{role Game|Companion, pid}`, `Injected{path, ok, error}`, `Output{role, stream, bytes}`, `Exited{role, code}`, `Fatal`.
- Requests: `Resume`, `Inject{entry}`, `Stop{grace_ms}`.
- On EOF, winhost calls `TerminateJobObject` and exits.

## 11. Front, credentials, legacy fixed-origin mode

**`front::SessionFront`.** One loopback listener on an OS-assigned port per engine.

- It routes `/s/<session_key>/…` (the key is 128 random bits) to that session's upstream and strips the prefix.
- It checks Host and Origin.
- Upstreams:
  - **Embedded:** `Ready.http_port`, with the `X-Reboot-*` headers added.
  - **Local / Remote:** the configured upstream. TLS is verified against system trust, a host once seen on https is never downgraded, and plain http needs a remembered `ConfirmUnencryptedUpstream` answer.
- The upstream allow-list is the backend host plus hosts learned from hotfix and ticket responses. Bodies are not rewritten.
- **Ticket swap.** Only `POST /account/api/oauth/token` carrying a session ticket is swapped, and only for password-backed upstreams. On Linux, redemption also requires the peer socket's uid to equal the engine uid (`ILoopbackPeerInspector`).

**`identity::plan_login`.** The credential depends on the target:

- **Embedded:** `-AUTH_TYPE=exchangecode` when the catalog flags the build, otherwise `epic`. The password is a launch credential minted by the backend.
- **Remote Reboot backend:** a password grant upstream, then an exchange code.
- **Other remote backends:** `epic` with a session ticket that the front swaps.
- **`legacy_argv`:** the real password, only with a custom auth DLL that targets a hosted backend. It is opt-in and shows a warning.

**`NetMode::LegacyFixed`** applies when the custom auth DLL is configured.

- The front binds `127.0.0.1:3551` unprefixed, plus `127.0.0.1:80` on Windows only.
- Only one such session runs at a time.
- An occupied port gives `PortBusy{port, owner, owned_by_us}`. Nothing is killed.
- On macOS and Linux the session raises `XmppUnavailable{ThirdPartyAuthDll}`.
- Our client DLL is still injected, with `auth_redirect=false`. The custom DLL is injected early.
- The support tier is Untested.

## 12. Flows

### Play (op, Detached)

1. `Play.start(PlayRequest{build_id, target, auto_server, backend_target?, custom_args})` validates synchronously.
2. Preflight:
   - layout (`builds`);
   - support verdict (Blocked fails; Untested raises `ConfirmUntested`);
   - payload and runtime pinned and verified (`components`, `compat`);
   - OS session or display;
   - identity;
   - secrets, raising `NeedsSecret` if one is missing;
   - backend lease and health (`backend`);
   - LegacyFixed constraints (`injection`).
3. Target:
   - a browser server goes through Resolve, `ConfirmJoin`, an optional `NeedsJoinPassword`, then JoinGrant;
   - a custom address is resolved now and probed (a probe failure is a warning only);
   - the linked auto-server calls `Host.start(auto profile, linked_to)`, which is Unlisted.

   The target is recorded for `ResolveMatchTarget`.
4. Credential plan, then `ConfigureSession`, then front route registration, then the game-channel token.
5. Launch plan:
   - argv: the fixed tokens, including the caldera literal; then `-AUTH_*`; then the custom args, parsed with quoting. An empty string never becomes an argument.
   - env from `EnvBuilder`.
   - cwd = the shipping exe's directory.
   - `InjectionPlan`.
6. `ISessionHost.launch` (Win32SessionHost or WineSessionHost) spawns the game suspended with its companions. It injects, resumes and emits `Spawned` (plus `ForegroundHint`).
7. The session then follows game-channel events: Hello, `Loaded`, `RedirectReady`, `LoggedIn` → Running. The op completes with the SessionId.
8. Stop: `Shutdown`, a 5 s grace, then the Job is killed. Then the route, lease, holds and linked server are released.

### Host (op, Detached)

1. `ProfileBusy` if the profile already has a live session; `HostLimitReached` at the limit (default 4).
2. Get the description (from the cache, or by running `--describe`). Check the build against `supports[]` and the SupportPolicy host cell.
3. `HostPortAllocator` reserves a block sized by `sockets[]`:
   - `Pinned(P)` gives {P, P+1, …} and fails with `PortBusy{owner}`;
   - `Auto(7777-7786)` takes the lowest free block;
   - every port is preflighted with a UDP wildcard bind without SO_REUSEADDR, telling EADDRINUSE apart from EACCES.
4. Spawn, then Hello, then Welcome{ServerConfig}. Then `Listening`, or `ListenFailed`, which kills the server: Auto moves to the next block, Pinned fails.
5. Readiness:
   - `IPortInspector` confirms the child pid owns each port;
   - the loopback rbsb probe is answered;
   - the deadline is 120 s, and by default the session keeps running unpublished on timeout.
6. Port mapping covers the whole block: UPnP or NAT-PMP, a 3600 s lease renewed at half the lease, error 725 handled as lease 0, 718 as another external port. A failure is only a diagnostic.
7. `publish` sends HostRegister with `game_port` = the granted external port, or the bound port.
8. On `MatchEnded`, `MatchEndPolicy` runs on a cancellable `Restarting` timer: `Reset` when `in_process_reset`, otherwise a respawn on the same block.
9. Stop: unpublish, unmap, `Shutdown{grace}`, then kill.

### Backend lease

1. The first lease starts the backend through a single-flight start. Re-entrant toggles queue.
2. The engine waits for `Ready`, then registers accounts.
3. After a crash: restart with backoff, then replay `RegisterAccount` and `ConfigureSession` for live sessions, using a generation counter.
4. The last lease, with no user pin, stops the backend.
5. A requested stop is never reported as a crash.
6. A Reset stops the backend first.
7. Remote and Local targets spawn nothing.

### Install (op, Detached)

1. Catalog entry.
2. Destination (`IDiskInfo` volumes and free space).
3. One resumable download stream into `<dest>/.reboot-staging`.
4. libarchive extract.
5. PE version detection.
6. Transactional library registration.
7. Staging is removed on success, cancel or failure.

## 13. Version streams

| Stream | Visibility | Rule |
|---|---|---|
| App version (SemVer) | public | One per release, shared by the engine, client library, SDK, UIs, CLI, `reboot-backend` and `reboot-game-server` |
| C ABI `RB_ABI_MAJOR.MINOR` | public | Before v1, promised within one release only (§9.0). Clients refuse a major mismatch. Minor stays additive by convention (header diff) |
| `reboot.api.v1` (`RB_SCHEMA_FINGERPRINT`) | public | Before v1, promised within one release only; `buf breaking` stays built. The SDK compares the fingerprint with the engine's |
| CLI `--json` | public | Stable, documented keys; the automation contract |
| Engine IPC | private | Exact build match gives Full. The bootstrap subset is frozen and is the only cross-release promise |
| Backend / game-server control | private | Exact `protocol` integer. Both ship in the app package |
| Game-control `payload_abi` | private | Exact integer. The manifest pins the payload per launcher version |
| Payload version | private | Own channel. Accepted only with a matching `payload_abi` |
| Manifest and catalog schema | signed data | Unknown manifest major is refused. Unknown catalog schema falls back to the cached or bundled copy |
| Store documents | private | `schema` per document. Additive keys never bump it. A reshape bumps it after a `.v<N>` backup. A newer schema opens read-only |
| rbsb/1 `proto_minor` | external | Owned by server-browser |

## 14. Testability

1. **Unit tests.** Every package has unit tests on `core/testing` fakes, with `ManualClock` and `ManualExecutor`. They include the chaos races (complete vs cancel vs deadline), the Resync path, the slow consumer and the UserRequest CAS.
2. **Contract tests.**
   - Golden vectors compare `reboot.api.v1` against protoc output, and the contracts against fixed byte vectors.
   - `reboot-fake-backend` and `reboot-fake-game-server` are real executables with scripted behaviour. They exercise ChildSupervisor, Job/pgroup containment, stdio framing and liveness across a real process boundary.
   - In-process peers (`FakeClientDll`, `FakeWinhost`) drive the engine through full play and host flows.
   - The conformance suites rerun unchanged against the phase-2 and phase-3 binaries.
3. **SDK tests.** The SDK's Catch2 suite goes through the real C ABI (`reboot_client`) against the engine rig with the fake backend, over the real OS endpoint: calls, ops with cancel, events through the wake hook, `LiveList` resync, reconnect, errors and secrets.
4. **SDK consumer check.** A minimal consumer is built from only the SDK headers and the `reboot_client` DLL: with the dynamic CRT (`/MD`) on Windows and with the system toolchain on Linux and macOS. It keeps the SDK's closure honest.
5. **Port conformance.** One suite per port, run as `reboot_os_<os>_conformance` against the real adapters on each OS's CI runner. It covers atomic replace, lock exclusivity, watcher delivery, process tree kill, IPC peer rejection and pipe squatting.
6. **Fuzzing.** The IPC and contract frames, `LineReader`, the PE version reader, `parse_deep_link` and `parse_host_port`.
7. **CI gates.** The ctest label `gate` marks the last three; each OS job runs them in their own step (`ctest -L gate`) after the other tests (`-LE gate`).
   - the language floor on three toolchains (`REBOOT_FLOOR_CHECK`);
   - the link DAG and include check (`dag_check`), including the lean-consumer rule (§4.2);
   - the `reboot_client` import check (part of `dag_check`);
   - message catalog completeness: `l10n_catalog_complete` runs `reboot-l10n check l10n/en.json`, which compiles every package's `messages.cpp` and fails on a missing, orphaned or mismatched id, on a `MessageId` literal no package registers, or on an SDK English catalog that differs from the registry. `reboot-l10n render l10n/en.json <launcher root>` regenerates the committed `en.json` and `core/sdk/include/reboot/sdk/english_catalog.hpp`;
   - the secret grep: `reboot_engine_secret_gate` (`core/engine/gates`) runs host and play flows on the engine rig with a known backend password and join password, collects the tokens and credentials the engine hands its children (the game's `-AUTH_PASSWORD`, `REBOOT_CTL_TOKEN`, the game server's service token), then greps the LogRing, the session log files, every entry of a `Logs.export` archive and every file under the data and cache roots;
   - the data root is never inside the Velopack package: `data_root_outside_package` (Windows and macOS; Linux installs have no package) copies `reboot-engine` and `reboot-data-root-probe` into a Velopack layout or an app bundle, checks the default data, cache and logs roots are outside it, and checks the engine refuses a `REBOOT_LAUNCHER_HOME` inside it (also through `..` and, on Windows, in another case) with `storage.root_inside_package`.
8. **Wine integration.** The real winhost (`payload/winhost`) under pinned Kron4ek Wine on Linux CI, with `reboot-fake-game`. An engine SIGKILL must end in winhost EOF and then the Job kill. A second job runs the same winhost through pinned umu-launcher and GE-Proton, with the Steam Linux Runtime umu downloads, under a private `$XDG_RUNTIME_DIR` tmpfs, so winhost is proven to read its token file through the read-only `PRESSURE_VESSEL_FILESYSTEMS_RO` share inside the container, and the file is proven gone once winhost connects. On the Linux runner the engine tests also run a Wine-shaped composed play flow: a fake runtime archive, a fake wineboot seeding the prefix, and `FakeWinhost` over the real game channel.

## 15. Build order

0. **server-browser prerequisites.** `sb_contract` INTERFACE target; MSVC-clean `features.hpp` (portable `SB_LIKELY`/`SB_NOINLINE`, no GCC-only builtins); StreamFramer yielding the raw u64 frame type with no codec or messages include; `version_bucket` and `valid_text` moved to a header with no registry dependency.
1. **Foundation** (`core/base`, `core/foundation`, `core/ports`, `core/contracts`, the cmake helpers) and the CI floor job.
2. **In parallel:** `posix`, the OS `platform` and `ipc` packages, `logging`, `trust`, `storage`, `api`, `testing`.
3. `ipc`, `client`, `api_dispatch`, `engine` skeleton (bootstrap subset end to end).
4. `net`, `components`, `catalog`, `support`, `builds`, `ux`, `integration`.
5. `process`, `secrets`, `identity`, `backend`, `front`, `gameserver`, `sessions`, `host`, `publish`, `browser`, against the fake executables and a staging edge.
6. `injection`, `game_channel`, `compat`, `play`, `win32session`, `payload/winhost`, the runners.
7. `updates`, then the full engine composition.
8. `sdk`, then the clients and the CLI over it.

## 16. Capability coverage

150 of the 178 ids are assigned, each to exactly one package. The other 28 are unassigned with reasons, which the `unassigned` list gives:

- server-DLL and exe-patch items, superseded by owner-2;
- upstream mirrors and Discord-CDN acquisition (owner-4, owner-5);
- backend internals (phase 2);
- dropped client-side password crypto;
- dead Supabase and WebSocket services;
- UI and CLI items (phase 4, over `rb::sdk`);
- process notes;
- the isBlankOrEmpty helper.

## 17. Open items for the owner

1. **Linux release builder.** gcc-toolset-15 does not exist for EL8. The options:
   - (a) a self-built GCC 15 on AlmaLinux 8;
   - (b) GCC 15 with a glibc 2.28 sysroot and static libstdc++ on a newer host;
   - (c) EL9, which raises the floor to glibc 2.34.

   This affects only the image.
2. **Client DLL schedule.** Proposed: build `rb_client.dll` in `launcher/payload/client/` alongside phase 3, next to winhost and built by the same job. The core ships the engine side, winhost, the contract and `FakeClientDll`. Until the DLL exists, real play works only with a custom auth DLL in LegacyFixed mode.
3. **Game-server inputs.** Does `reboot-game-server` read the selected build (`build_root`), or only its version? This decides whether hosting requires an installed build.
4. **Steam Deck Game Mode** (the ClientExec attach path and window tagging). Deferred out of v1 unless the owner makes it a gate. The `ISessionHost` seam leaves room for it.
5. **Default host port policy.** Recommended: the default profile is `Pinned(7777)`; new profiles and the auto profile use `Auto(7777-7786)`.
6. **Concurrent-host limit.** Default 4, matching the rbsb per-IP cap.
7. **Backend defaults.** Does a rename carry the locker over?
8. **Windows injection location.** Stays in the engine unless the AV/Smart App Control spike, run against the windowless engine, shows that winhost on Windows helps.
9. **Windows Firewall** prompt for `reboot-game-server`: an installer-time rule, or the first-run OS prompt.
10. **Game-server delivery.** In the app package and signed with the app (recommended), or as its own component channel.
11. **Linux client toolkit.** Undecided; the client compiles `rb::sdk` and plugs its loop into the SDK's `Dispatcher`.
12. **SDK licence.** `core/sdk` is GPL-3.0-or-later with the core unless the owner extends the Apache-2.0 tier of the C ABI header and schema to it.


# Conventions

LANGUAGE
- C++26, restricted to features that MSVC (latest), Apple Clang with libc++, and GCC 15 all accept. The CI floor job is the authority. CMAKE_CXX_EXTENSIONS is OFF.
- Never used: named modules, import std, std::inplace_vector, std::flat_map, std::jthread, std::stop_token, std::execution, and contracts or reflection outside the sb SB_* macros. Use the foundation's boost::container::flat_map alias, UniqueFunction and CancelToken instead.
- No protobuf runtime anywhere in C++. Public reboot.api.v1 messages are generated aggregates. Private wire messages are hand-written aggregates in core/contracts. Both encode with the sb codec, where field number = declaration index + 1.
- The SDK headers are compiled by client toolchains too, so they use only what those toolchains accept (C++23 with std::expected) and nothing from outside its closure (§9.3).

FILES AND NAMES
- Files are .hpp/.cpp, snake_case. One main type per header, with the header named after it (e.g. backend_service.hpp).
- Every package directory has the same shape:
  - <path>/CMakeLists.txt (calls reboot_add_package, or defines an INTERFACE target for a header-only package)
  - <path>/include/reboot/<area>/*.hpp (public headers)
  - <path>/src/*.cpp and private src/*.hpp
  - <path>/tests/*.cpp (Catch2)
- <area>:
  - core packages: the package name, e.g. reboot/backend/backend_service.hpp.
  - OS packages: os_windows/<pkg>, os_macos/<pkg>, os_linux/<pkg>, e.g. reboot/os_linux/ipc/unix_socket_listener.hpp. winhost keeps os_windows/winhost under payload/winhost.
  - Foundation: reboot/base/*.hpp, reboot/foundation/*.hpp, reboot/ports/*.hpp, reboot/contracts/*.hpp.
  - The public C header is core/client/include/reboot/client.h; the SDK is reboot/sdk/*.hpp.
- Includes:
  - Always angle-free quoted paths rooted at an include dir: #include "reboot/net/http_client.hpp".
  - server-browser headers are included as "wire/codec.hpp", "client/view_mirror.hpp" and so on, through sb_contract.
  - A package never includes another package's src/.
- Namespaces:
  - Packages use rb::<area>, e.g. rb::backend, rb::game_channel. The SDK uses rb::sdk.
  - OS packages use rb::os_windows::<pkg>, rb::os_macos::<pkg>, rb::os_linux::<pkg>. Never a bare 'linux' identifier.
  - Base and foundation use rb (value types, diag, async, events, log, paths), rb::ports and rb::contracts::<channel> (ipc, backend, game_server, game_client, winhost, common).
- Identifier case:
  - Types are PascalCase; functions, variables and members are snake_case; private members end with _.
  - Constants are kPascalCase. Enums are enum class with PascalCase enumerators. Macros are REBOOT_UPPER_CASE.
- Port interfaces are IPascalCase with a virtual destructor and pure virtual methods. Adapters are named after what they use (JobProcessLauncher, KeychainSecretStore). Fakes are named Fake<Thing> and live in core/testing.
- CMake:
  - Targets are reboot_<area with / replaced by _>, with alias reboot::<same>, e.g. reboot_os_linux_ipc.
  - Executables are kebab-case: reboot-engine, reboot-winhost, reboot-fake-backend, reboot-fake-game-server, reboot-fake-game.
  - A package may only list in DEPS the packages named in its 'Depends on' line. The foundation is implicit.
  - Sources are listed explicitly.

HEADERS
- #pragma once.
- A header includes only what it uses. Forward-declare port interfaces where possible.
- No 'using namespace' in headers.
- Inline only trivial code and templates, except in header-only packages (base, api, sdk), which define everything they declare.
- No OS headers (windows.h, unistd.h and so on) in any core header. Only core/posix, OS packages, payload and foundation/src/random_os.cpp include them.
- On Windows, include through one private src/win32.hpp per OS package that defines WIN32_LEAN_AND_MEAN and NOMINMAX.

ERRORS
- Every fallible function returns rb::Result<T> (std::expected<T, Diagnostic>), or Result<void>.
- Inside a package, typed error enums or structs (PortBusy{port, owner}, InstallError) may be used. They convert to Diagnostic at the service boundary through a to_diagnostic() overload.
- Each MessageId is declared once with REBOOT_MESSAGE(dotted.id, "English template {arg}") in the package's src/messages.cpp. Ids are '<area>.<snake_case>', are never reused, and every placeholder matches a named arg. A header-only package names its ids as MessageId literals, and the package that owns the English registers them.
- Core never builds user-facing sentences.
- Exceptions:
  - Not used for control flow.
  - Third-party exceptions (Boost.JSON, Asio) are caught at the adapter that calls the library and converted to Diagnostic.
  - Worker and adapter thread entry points catch everything and report internal.bug.
  - Every C ABI function is noexcept.
- OS error codes travel as SystemError{Host or GuestWindows, code}, never as text.

ASYNC AND THREADING
- All domain state lives on the engine Strand. Service objects are not thread-safe and are called only on the strand.
- Blocking work (file I/O, hashing, extraction, PE reads, DNS without a cancellable API, WMI) goes through WorkerPool::submit with a CancelToken, and its result is posted back to the strand.
- Adapters (Asio sockets, MsQuic callbacks, pipe readers) never touch domain state. They post decoded values to the strand.
- Time comes only from IClock and TimerService. Never call std::chrono::*::now() or sleep in domain code.
- Long work is an op:
  - start_xxx(...) validates synchronously and returns Result<OpHandle>.
  - The work reports Progress and ends with exactly one Outcome through Operation<T>::complete.
  - Every op has an OpKind with a deadline.
  - To ask a human, raise a UserRequest. Never block waiting for one.
- Events are published on the EventBus with a kind registered in foundation/events.hpp. Domain packages define their own event payload structs. Only the engine's ApiRouter converts them to API messages.

OWNERSHIP AND MEMORY
- Constructor injection only, no service locators or singletons. The process-wide Logger is the single exception.
- Dependencies are taken as references (non-null, borrowed, outliving the service).
- Owned members are values or std::unique_ptr.
- std::shared_ptr only where an Asio or MsQuic handler must extend the lifetime of a connection object.
- The engine composition root owns every service and constructs them in dependency order. Destruction runs in reverse.
- Secrets are Secret<T>/SecretString: move-only and wiped on destruction. They are registered with the Redactor when loaded, never logged and never formatted (formatting yields ***).
- Paths are NativePath (std::filesystem::path). Never round-trip a path through a lossy string.
- C ABI outputs are allocated by reboot_client and freed only by rb_buffer_release, which wipes them. Inputs are borrowed for the duration of the call.

TESTS
- Each package's tests use core/testing fakes, ManualClock and ManualExecutor. They use no real network, no sleeping and no real home directory.
- Tests that need real OS behaviour belong to the OS package's conformance run.
- Golden files live under <path>/tests/data/.

COMMENTS AND STYLE
- Comments only where a point is non-obvious, each one short line explaining why. No banner comments, no restating the code, and no commented-out code.
- Names say what things are. No speculative features or unused parameters.
- clang-format and the server-browser warning set (RebootWarnings.cmake) apply, and warnings are errors.
