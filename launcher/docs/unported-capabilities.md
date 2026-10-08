# Unported capabilities

Every capability id from the 10.x audit is either cited by a `Capabilities:` or `Covers` comment
in this tree, or listed here with the reason it has no code. The owner decisions are numbered as
in the phase-1 architecture: 2 (host runs our standalone game server), 4 (no backward
compatibility with the old launcher), 5 (we own every DLL and the backend; a user custom auth DLL
stays through `NetMode::LegacyFixed`) and 7 (phases: core now, then backend, game server, UIs).

An id leaves this list only when code that implements it cites it.

## Superseded by our game server (owner decision 2)

| Id | Capability | Disposition |
|---|---|---|
| `game-builds.headless-patch` | Shipping executable binary patching | Hosting runs `reboot-game-server`, so the shipping exe is never patched. |
| `hosting.headless` | Headless game server option | Our game server is headless by construction; there is no option to store. |

## Upstream DLLs and mirrors (owner decision 5)

| Id | Capability | Disposition |
|---|---|---|
| `dll-injection.reboot-dll-updater` | Reboot game-server DLL updater | No game-server DLL is injected; our client DLL (`rb_client.dll`) ships as a launcher payload. |
| `dll-injection.+55` | Legacy CLI DLL and embedded-backend acquisition (Discord CDN) | Nothing is downloaded from upstream sources. |
| `dll-injection.+83` | Reboot DLL fallback mirrors | No mirrored reboot.dll. |

## Old launcher state, install and history (owner decision 4)

| Id | Capability | Disposition |
|---|---|---|
| `auth-backend.lawinserver-state` | LawinServer persisted state | 10.x backend data is not imported; our backend keeps its own state. |
| `dll-injection.+11` | cobalt.dll as the shipped 10.0.9 auth DLL | No upstream auth DLL ships (decision 5) and stored 10.x auth paths are not read. A user's own auth DLL goes through the custom auth slot. |
| `dll-injection.+69` | cobalt.dll to sinum.dll switch and its migration hazard | 10.x DLL settings are not migrated. |
| `dll-injection.+70` | Game-server DLL update interval setting | Neither the setting nor the updater it tuned exists (decision 5); nothing is migrated. |
| `dll-injection.+81` | Persisted DLL update interval (`v3_dll_storage`) | Same as `dll-injection.+70`. |
| `dll-injection.+82` | Stored default DLL paths needing mapping on migration | 10.x DLL paths are not read. |
| `game-builds.+71` | 10.0.9 build catalog on public.simplyblk.xyz | 10.x catalogs and build names are not read; builds come from our signed catalog. |
| `game-builds.+79` | 10.0.9 catalog contents (85 builds, all available) | Same as `game-builds.+71`. |
| `packaging-distribution.installer` | 10.x Windows installer and the OS state it writes | Its install, registry entries, URL scheme and Defender exclusion are left alone. Our installer is phase 4. |
| `packaging-distribution.+61` | 10.x branch topology and which code is released | Process note about the old repository; no code. |
| `packaging-distribution.+90` | CLI never shipped in 10.0.9 | No 10.x CLI store to import; no code. |

## Backend internals (owner decision 7, phase 2)

Core sees only the backend process contract (`core/contracts`).

| Id | Capability | Disposition |
|---|---|---|
| `auth-backend.lawinserver` | Bundled LawinServer backend | Phase 2 backend program. |
| `auth-backend.lawinserver-version-routing` | Per-build routing on the User-Agent; CloudStorage path traversal | Phase 2 backend internals. |
| `auth-backend.+15` | CloudStorage hotfixes and HTTP contract details | Phase 2 backend internals. |

## Dropped features

| Id | Capability | Disposition |
|---|---|---|
| `hosting.password-crypto` | Server password protection (bcrypt, AES-encrypted IP) | Dropped (drop-features A.1): rbsb/1 checks passwords server-side, and core has no client crypto. |
| `hosting.+23` | Non-standard HMAC block length in password crypto | Dropped with the client crypto; nothing is left to interoperate with. |
| `hosting.+32` | Same, for the legacy password and IP crypto | Same as `hosting.+23`. |
| `hosting.+66` | Same, for the server-IP encryption | Same as `hosting.+23`. |
| `server-browser.legacy-service` | Legacy server_browser_backend service | Retired; rbsb/1 replaces it. |
| `server-browser.+26` | Legacy Supabase and WebSocket wire protocols | Dead services; no launcher code speaks them. |

## UIs and CLI (owner decision 7, phase 4)

| Id | Capability | Disposition |
|---|---|---|
| `os-integration.window-theming` | Window management and theming | UI shell; core stores only the opaque frontend state. |
| `os-integration.+42` | Window management and Windows 11 detection | UI shell. |
| `os-integration.+43` | Desktop notifications (match end, auto restart) | Each UI renders them from the events core publishes. |
| `onboarding-ux-flows.+75` | Modal tracking (`inDialog`) | UI concern. |
| `onboarding-ux-flows.+77` | Onboarding sub-page navigation ids | UI concern. |
| `onboarding-ux-flows.cli-parser` | CLI argument parser and interactive prompts | Rust CLI. |
| `cli-ux.+57` | ConsoleParser grammar | Rust CLI. |
| `cli-ux.+58` | CLI menus, async dispatch and exit status | Rust CLI; core supplies `exit_code_for(Diagnostic)`. |
| `game-launch.+54` | Legacy 9.x CLI headless play/host runner and flags | Rust CLI flags; play and host are engine operations. |
| `packaging-distribution.+60` | CLI packaging and dependencies | Phase 4 packaging. |

## Not ported, no replacement

| Id | Capability | Disposition |
|---|---|---|
| `os-integration.elevated-start` | Elevated process start helper | Never called in 10.x; core never elevates. |
| `logging-diagnostics.+46` | Generic helpers (`isBlankOrEmpty` semantics) | Its space-only blank test is a 10.x quirk; nothing depends on it. |
| `packaging-distribution.refactor-compile-state` | Refactor branch does not compile | Process note; no code. |
| `packaging-distribution.+74` | Released clients depend on master-branch GitHub URLs | 10.x repository policy, not launcher code. |
