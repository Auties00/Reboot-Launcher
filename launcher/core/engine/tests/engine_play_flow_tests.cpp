#include <openssl/evp.h>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstring>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "api_convert.hpp"
#include "engine_rig.hpp"
#include "reboot/api/v1/components.hpp"
#include "reboot/api/v1/engine.hpp"
#include "reboot/api/v1/library.hpp"
#include "reboot/api/v1/play.hpp"
#include "reboot/api/v1/sessions.hpp"
#include "reboot/api/v1/support.hpp"
#include "reboot/components/manifest_platform.hpp"
#include "reboot/contracts/game_client.hpp"
#include "reboot/contracts/winhost.hpp"
#include "reboot/foundation/sha256.hpp"
#include "reboot/foundation/text.hpp"
#include "reboot/ports/runner.hpp"
#include "reboot/testing/fake_client_dll.hpp"
#include "reboot/testing/fake_runner_platform.hpp"
#include "reboot/testing/fake_session_control.hpp"
#include "reboot/testing/fake_winhost.hpp"
#include "reboot/trust/key_ring.hpp"
#include "reboot/trust/signed_document.hpp"
#include "reboot/trust/signed_document_kind.hpp"

using namespace rb;
using namespace rb::engine;
using namespace rb::engine::test;
using namespace std::chrono_literals;

namespace {

struct PkeyDeleter {
    void operator()(EVP_PKEY* key) const noexcept { EVP_PKEY_free(key); }
};
struct MdCtxDeleter {
    void operator()(EVP_MD_CTX* context) const noexcept { EVP_MD_CTX_free(context); }
};

// Signs as the release pipeline does: the ReleaseManifest context prefix, then the body.
class ManifestSigner {
public:
    ManifestSigner() : key_(EVP_PKEY_Q_keygen(nullptr, nullptr, "ED25519")) {
        REQUIRE(key_);
        std::size_t size = public_key_.size();
        REQUIRE(EVP_PKEY_get_raw_public_key(key_.get(), public_key_.data(), &size) == 1);
    }

    [[nodiscard]] std::string signature_file(std::string_view body) const {
        const std::string_view prefix = trust::signature_context(trust::SignedDocumentKind::ReleaseManifest);
        std::vector<u8> message(prefix.begin(), prefix.end());
        message.insert(message.end(), body.begin(), body.end());
        const std::unique_ptr<EVP_MD_CTX, MdCtxDeleter> context(EVP_MD_CTX_new());
        REQUIRE(EVP_DigestSignInit(context.get(), nullptr, nullptr, nullptr, key_.get()) == 1);
        trust::Ed25519Signature signature{};
        std::size_t size = signature.size();
        REQUIRE(EVP_DigestSign(context.get(), signature.data(), &size, message.data(), message.size()) == 1);
        return "ed25519 " + trust::key_id_of(public_key_) + " " + to_hex(signature) + "\n";
    }

    [[nodiscard]] trust::PinnedKeys keys() const { return trust::PinnedKeys{public_key_, std::nullopt}; }

private:
    std::unique_ptr<EVP_PKEY, PkeyDeleter> key_;
    trust::Ed25519PublicKey public_key_{};
};

constexpr std::string_view kClientDll = "rb_client.dll contents";
constexpr std::string_view kWinhost = "reboot-winhost.exe contents";
constexpr u32 kChangelist = 13498980;
constexpr std::string_view kRuntimeUrl = "https://cdn.test/runtime/kron-11.tar";

[[nodiscard]] std::string sha_hex(std::string_view content) {
    return to_hex(sha256(std::span(reinterpret_cast<const u8*>(content.data()), content.size())));
}

[[nodiscard]] std::string remote_file(std::string_view role, std::string_view name, std::string_view content) {
    return R"({"role":")" + std::string(role) + R"(","urls":["https://cdn.test/payload/)" + std::string(name) +
           R"("],"sha256":")" + sha_hex(content) + R"(","size":)" + std::to_string(content.size()) + "}";
}

// A Wine runtime for this build's platform, whose archive is served at kRuntimeUrl.
[[nodiscard]] std::string runtime_entry(std::string_view archive) {
    const components::ManifestPlatform platform = components::build_platform();
    const std::string_view os = platform.os == components::ManifestOs::Windows ? "windows"
                                : platform.os == components::ManifestOs::MacOs ? "macos"
                                                                               : "linux";
    const std::string_view arch = platform.arch == components::ManifestArch::Arm64 ? "arm64" : "x64";
    return R"({"id":"kron-11","kind":"kron_wine","version":"11.0","archive":{"urls":[")" + std::string(kRuntimeUrl) +
           R"("],"sha256":")" + sha_hex(archive) + R"(","size":)" + std::to_string(archive.size()) +
           R"(},"platform":{"os":")" + std::string(os) + R"(","arch":")" + std::string(arch) + R"("}})";
}

// One payload and `runtimes`, valid long after the rig's wall clock.
[[nodiscard]] std::string manifest_text(std::string_view client_dll, std::string_view runtimes = "") {
    return R"({"schema":1,"serial":3,"expires_unix_ms":4000000000000,"apps":[],"payloads":[{"version":"1.0.0","payload_abi":)" +
           std::to_string(contracts::game_client::kPayloadAbi) + R"(,"files":[)" +
           remote_file("client_dll", "rb_client.dll", client_dll) + "," + remote_file("winhost", "reboot-winhost.exe", kWinhost) +
           R"(]}],"runtimes":[)" + std::string(runtimes) + "]}";
}

// The smallest PE32+ image the prefix's VC++ check reads: no sections and no imports.
[[nodiscard]] std::string minimal_pe() {
    std::string image(0x200, '\0');
    const auto put = [&image](std::size_t at, u32 value, std::size_t bytes) {
        for (std::size_t i = 0; i < bytes; ++i) image[at + i] = static_cast<char>((value >> (8 * i)) & 0xFF);
    };
    put(0x00, 0x5A4D, 2);      // MZ
    put(0x3C, 0x40, 4);        // e_lfanew
    put(0x40, 0x00004550, 4);  // PE\0\0
    put(0x44, 0x8664, 2);      // AMD64
    put(0x54, 0xF0, 2);        // the optional header's size
    put(0x58, 0x20B, 2);       // PE32+
    put(0x58 + 108, 16, 4);    // data directories
    return image;
}

// A ustar archive of `files`, as the runtimes' tarballs are; the store's extractor reads any format.
[[nodiscard]] std::string tar_of(const std::vector<std::pair<std::string, std::string>>& files) {
    const auto octal = [](std::string& header, std::size_t at, std::size_t width, u64 value) {
        for (std::size_t i = width - 1; i-- > 0; value >>= 3) header[at + i] = static_cast<char>('0' + (value & 7));
        header[at + width - 1] = '\0';
    };
    std::string out;
    for (const auto& [name, content] : files) {
        std::string header(512, '\0');
        std::memcpy(header.data(), name.data(), name.size());
        octal(header, 100, 8, 0755);
        octal(header, 108, 8, 0);
        octal(header, 116, 8, 0);
        octal(header, 124, 12, content.size());
        octal(header, 136, 12, 0);
        header[156] = '0';
        std::memcpy(header.data() + 257, "ustar", 6);
        std::memcpy(header.data() + 263, "00", 2);
        std::memset(header.data() + 148, ' ', 8);
        u64 sum = 0;
        for (const char c : header) sum += static_cast<u8>(c);
        // Six digits and a NUL; the field's last byte stays a space.
        octal(header, 148, 7, sum);
        out += header;
        out += content;
        out.append((512 - content.size() % 512) % 512, '\0');
    }
    out.append(1024, '\0');
    return out;
}

[[nodiscard]] std::string env_of(const ports::EnvBlock& env, std::string_view name) {
    for (const auto& [key, value] : env.vars)
        if (key == name) return value;
    return {};
}

// Used by the Linux-only Wine flow.
[[nodiscard, maybe_unused]] std::string text_of(const contracts::winhost::Bytes& utf16le) {
    std::u16string text;
    for (std::size_t i = 0; i + 1 < utf16le.size(); i += 2)
        text.push_back(static_cast<char16_t>(utf16le[i] | utf16le[i + 1] << 8));
    return utf16_to_utf8(text);
}

// Native, as on Windows; or Wine, as on Linux: no session host, the Wine runner over
// FakeRunnerPlatform, a runtime archive in the manifest, and the runner played by play_runner.
enum class Shape : u8 { Native, Wine };

[[nodiscard]] testing::FakeHttpResponse body_of(std::string_view content) {
    testing::FakeHttpResponse response;
    response.body.assign(content.begin(), content.end());
    return response;
}

struct PlayRig {
    explicit PlayRig(Shape shape = Shape::Native) {
        // The prefix's VC++ check reads the client DLL's imports.
        const std::string client_dll = shape == Shape::Wine ? minimal_pe() : std::string(kClientDll);
        const std::string runtime_archive = tar_of({{"bin/wine", "#!wine"}});
        const std::string manifest = manifest_text(client_dll, shape == Shape::Wine ? runtime_entry(runtime_archive) : "");
        write_file(rig.install.bundled_manifest, manifest);
        NativePath signature = rig.install.bundled_manifest;
        signature += ".sig";
        write_file(signature, signer.signature_file(manifest));
        rig.http.route("GET", "https://cdn.test/payload/rb_client.dll", body_of(client_dll));
        rig.http.route("GET", "https://cdn.test/payload/reboot-winhost.exe", body_of(kWinhost));
        rig.http.route("GET", std::string(kRuntimeUrl), body_of(runtime_archive));

        build_root = rig.scratch->path() / "builds" / "12.41";
        write_file(build_root / "FortniteGame" / "Binaries" / "Win64" / "FortniteClient-Win64-Shipping.exe", "MZ game");

        if (shape == Shape::Wine) wine_shaped();
        rig.serve_backend();
        rig.serve_game_server();
        if (rig.session_host != nullptr) rig.session_host->on_resume([this](testing::FakeSessionControl& control) {
            control.spawn_all();
            for (const ports::InjectEntry& entry : control.launch().inject) control.emit(ports::Injected{entry.path, true, std::nullopt});
            Result<testing::GameControlBootstrap> bootstrap = control.bootstrap();
            REQUIRE(bootstrap);
            testing::FakeClientDllScript script;
            script.game = contracts::game_client::GameBuild{"12.41", kChangelist};
            script.after_welcome = dll_steps;
            dll = std::make_unique<testing::FakeClientDll>(rig.strand, rig.clock, std::move(script));
            REQUIRE(dll->connect(rig.io, *bootstrap));
        });

        EngineOverrides overrides;
        overrides.manifest_keys = signer.keys();
        rig.boot(EngineOrigin::Foreground, std::move(overrides));
        client = rig.connect();
        rig.auto_accept(*client, {api::UserRequestKind::ConfirmUntested});
    }

    ~PlayRig() {
        // The DLL's socket callbacks run on the rig's I/O thread, so it goes before the rig stops.
        if (dll) dll->disconnect();
        if (winhost) winhost->disconnect();
    }

    void wine_shaped() {
        auto processes = std::make_unique<testing::ScriptedProcessLauncher>(rig.strand, rig.clock, testing::FakeOs::Linux);
        rig.processes = processes.get();
        rig.platform.processes = std::move(processes);
        rig.platform.session_host.reset();
        rig.session_host = nullptr;
        auto runner = std::make_unique<testing::FakeRunnerPlatform>(std::vector{ports::RunnerKind::Umu, ports::RunnerKind::Wine});
        ports::RuntimeLayout layout;
        layout.entry = "bin/wine";
        runner->set_layout(ports::RunnerKind::Wine, std::move(layout));
        rig.platform.runner = std::move(runner);
        rig.processes->on_exe("wine", [this](testing::ScriptedChild& child) { return play_runner(child); });
    }

    // <runtime>/bin/wine: wineboot seeds the prefix, wineserver -k finds no server, and anything
    // else is reboot-winhost.exe, which connects once launch() has written its token to stdin.
    Result<void> play_runner(testing::ScriptedChild& child) {
        const std::vector<std::string>& args = child.launch().args;
        if (args == std::vector<std::string>{"wineboot", "-u"}) {
            ++wineboots;
            seed_prefix(NativePath(env_of(child.launch().env, "WINEPREFIX")));
            child.exit(ports::ChildExit{0, std::nullopt});
        } else if (args == std::vector<std::string>{"wineserver", "-k"}) {
            child.exit(ports::ChildExit{1, std::nullopt});
        } else {
            rig.strand.post([this, &child] { start_winhost(child); });
        }
        return {};
    }

    // What wineboot leaves: drive_c, system.reg, and the c: and z: drives PathMapper reads.
    static void seed_prefix(const NativePath& prefix) {
        std::filesystem::create_directories(prefix / "drive_c" / "users" / "steamuser");
        std::filesystem::create_directories(prefix / "dosdevices");
        std::filesystem::create_directory_symlink("../drive_c", prefix / "dosdevices" / "c:");
        std::filesystem::create_directory_symlink("/", prefix / "dosdevices" / "z:");
        write_file(prefix / "system.reg", "WINE REGISTRY Version 2\n");
    }

    void start_winhost(testing::ScriptedChild& runner) {
        Result<testing::GameControlBootstrap> bootstrap = testing::FakeWinhost::bootstrap_of(runner.launch(), runner.stdin_bytes());
        REQUIRE(bootstrap);
        testing::FakeClientDllScript game_dll;
        game_dll.game = contracts::game_client::GameBuild{"12.41", kChangelist};
        game_dll.after_welcome = dll_steps;
        testing::FakeWinhostScript script;
        script.game_client_dll = std::move(game_dll);
        winhost = std::make_unique<testing::FakeWinhost>(rig.strand, rig.clock, std::move(script));
        REQUIRE(winhost->connect(rig.io, *bootstrap));
    }

    [[nodiscard]] api::Build import_build() {
        api::LibraryImportRequest request;
        request.path = convert::path(build_root);
        request.name = "Chapter 2";
        request.version = api::GameVersion{12, 41, std::nullopt};
        request.changelist = kChangelist;
        Result<u64> op = rig.start(*client, api::kLibraryImport, request);
        REQUIRE(op);
        return EngineRig::completed<api::LibraryImportResponse>(rig.settle_op(*client, *op)).build;
    }

    void select(const api::Build& build) {
        REQUIRE(rig.call<api::LibrarySelectResponse>(*client, api::kLibrarySelect, api::LibrarySelectRequest{api::GameRole::Client, build.id}));
    }

    ManifestSigner signer;
    EngineRig rig;
    NativePath build_root;
    std::vector<testing::ClientDllStep> dll_steps = testing::default_client_dll_steps();
    std::unique_ptr<testing::FakeClientDll> dll;
    // Wine only: reboot-winhost.exe, and how often wineboot ran.
    std::unique_ptr<testing::FakeWinhost> winhost;
    int wineboots = 0;
    std::unique_ptr<testing::ApiTestClient> client;
};

}  // namespace

TEST_CASE("play flow: an imported build is planned, launched with our DLL, and reaches Running") {
    PlayRig p;
    const api::Build build = p.import_build();
    CHECK(build.version == api::GameVersion{12, 41, std::nullopt});
    p.select(build);

    const Result<api::PlayPlanResponse> plan =
        p.rig.call<api::PlayPlanResponse>(*p.client, api::kPlayPlan, api::PlayPlanRequest{api::PlayRequest{}});
    REQUIRE(plan);

    Result<u64> op = p.rig.start(*p.client, api::kPlayStart, api::PlayStartRequest{api::PlayRequest{}});
    REQUIRE(op);
    const api::PlayStartResponse started = EngineRig::completed<api::PlayStartResponse>(p.rig.settle_op(*p.client, *op));
    REQUIRE(p.dll);
    REQUIRE(p.dll->welcome());
    CHECK(p.dll->welcome()->session_id == started.session.uuid);
    CHECK(p.rig.backends_spawned == 1);
    REQUIRE(p.rig.session_host->last() != nullptr);
    CHECK(p.rig.session_host->last()->resumed());
    CHECK_FALSE(p.rig.session_host->last()->launch().inject.empty());

    const Result<api::SessionsGetResponse> session =
        p.rig.call<api::SessionsGetResponse>(*p.client, api::kSessionsGet, api::SessionsGetRequest{started.session});
    REQUIRE(session);
    CHECK(session->summary.kind == api::SessionKind::Play);
    CHECK(session->summary.phase == api::SessionPhase::Running);
    CHECK(session->detail.tier != api::SupportTier::Unknown);
    CHECK(session->detail.tier == plan->tier);

    // A second play session is refused while one runs.
    const Result<u64> second = p.rig.start(*p.client, api::kPlayStart, api::PlayStartRequest{api::PlayRequest{}});
    if (second) CHECK(p.rig.settle_op(*p.client, *second).failed);

    Result<u64> stop = p.rig.start(*p.client, api::kSessionsStop, api::SessionsStopRequest{started.session, 0});
    REQUIRE(stop);
    static_cast<void>(EngineRig::completed<api::SessionsStopResponse>(p.rig.settle_op(*p.client, *stop)));
    CHECK(p.rig.session_host->last()->stop_grace().has_value());
}

TEST_CASE("play flow: a build above the version cap is offered the opt-in, which Library.Update grants") {
    PlayRig p;
    const api::Build build = p.import_build();
    CHECK_FALSE(build.allow_above_cap);
    api::LibraryUpdateRequest newer;
    newer.id = build.id;
    newer.version = api::GameVersion{31, 0, std::nullopt};
    REQUIRE(p.rig.call<api::LibraryUpdateResponse>(*p.client, api::kLibraryUpdate, newer));

    api::SupportQueryRequest query;
    query.build = build.id;
    query.role = api::GameRole::Client;
    const Result<api::SupportQueryResponse> blocked =
        p.rig.call<api::SupportQueryResponse>(*p.client, api::kSupportQuery, query);
    REQUIRE(blocked);
    CHECK(blocked->tier == api::SupportTier::Blocked);
    CHECK(blocked->opt_in_available);

    api::LibraryUpdateRequest allow;
    allow.id = build.id;
    allow.allow_above_cap = true;
    const Result<api::LibraryUpdateResponse> allowed =
        p.rig.call<api::LibraryUpdateResponse>(*p.client, api::kLibraryUpdate, allow);
    REQUIRE(allowed);
    CHECK(allowed->build.allow_above_cap);

    const Result<api::SupportQueryResponse> opted =
        p.rig.call<api::SupportQueryResponse>(*p.client, api::kSupportQuery, query);
    REQUIRE(opted);
    CHECK(opted->tier == api::SupportTier::Untested);
    CHECK_FALSE(opted->opt_in_available);
    CHECK(std::ranges::any_of(opted->reasons,
                              [](const api::Diagnostic& reason) { return reason.id == "support.above_version_cap_opted_in"; }));
}

TEST_CASE("play flow: a game that exits before logging in fails the start") {
    PlayRig p;
    p.dll_steps = {contracts::game_client::Loaded{"fake", 1, contracts::game_client::BuildMatch::Exact}};
    p.select(p.import_build());
    Result<u64> op = p.rig.start(*p.client, api::kPlayStart, api::PlayStartRequest{api::PlayRequest{}});
    REQUIRE(op);
    REQUIRE(p.rig.strand.pump_until([&] {
        p.rig.accept_requests();
        return p.dll && p.dll->welcome().has_value();
    }));
    p.rig.session_host->last()->game_exits(0);
    const api::Outcome outcome = p.rig.settle_op(*p.client, *op);
    REQUIRE(outcome.failed);
    CHECK(outcome.failed->id == "play.exited_before_login");
}

// The prefix's dosdevices links need a POSIX file system; the Linux CI runner runs this.
#if defined(__linux__)
TEST_CASE("play flow: under Wine the runtime is fetched, the prefix booted and winhost runs the game to Running") {
    PlayRig p(Shape::Wine);
    const api::Build build = p.import_build();
    p.select(build);

    const Result<api::PlayPlanResponse> plan =
        p.rig.call<api::PlayPlanResponse>(*p.client, api::kPlayPlan, api::PlayPlanRequest{api::PlayRequest{}});
    REQUIRE(plan);
    for (const api::Diagnostic& blocker : plan->blockers) FAIL_CHECK(blocker.id);
    CHECK(plan->runner == api::RunnerKind::Wine);

    Result<u64> op = p.rig.start(*p.client, api::kPlayStart, api::PlayStartRequest{api::PlayRequest{}});
    REQUIRE(op);
    const api::PlayStartResponse started = EngineRig::completed<api::PlayStartResponse>(p.rig.settle_op(*p.client, *op));
    CHECK(p.wineboots == 1);
    REQUIRE(p.winhost);
    CHECK(p.winhost->resumed());
    testing::FakeClientDll* const game_dll = p.winhost->game_client_dll();
    REQUIRE(game_dll != nullptr);
    REQUIRE(game_dll->welcome());
    CHECK(game_dll->welcome()->session_id == started.session.uuid);

    // The game and our DLL reach winhost as Windows paths on the prefix's z: drive.
    const std::optional<contracts::winhost::SpawnGame> spawn = p.winhost->spawn_request();
    REQUIRE(spawn);
    CHECK(text_of(spawn->exe_utf16).starts_with("Z:\\"));
    CHECK(text_of(spawn->exe_utf16).ends_with("\\FortniteClient-Win64-Shipping.exe"));
    const std::vector<contracts::winhost::InjectSpec> injected = p.winhost->injected();
    CHECK(std::ranges::all_of(injected, [](const auto& entry) { return text_of(entry.path_utf16).starts_with("Z:\\"); }));
    CHECK(std::ranges::any_of(injected, [](const auto& entry) { return text_of(entry.path_utf16).ends_with("\\rb_client.dll"); }));

    const Result<api::SessionsGetResponse> session =
        p.rig.call<api::SessionsGetResponse>(*p.client, api::kSessionsGet, api::SessionsGetRequest{started.session});
    REQUIRE(session);
    CHECK(session->summary.kind == api::SessionKind::Play);
    CHECK(session->summary.phase == api::SessionPhase::Running);
    CHECK(session->detail.tier == plan->tier);

    Result<u64> stop = p.rig.start(*p.client, api::kSessionsStop, api::SessionsStopRequest{started.session, 0});
    REQUIRE(stop);
    static_cast<void>(EngineRig::completed<api::SessionsStopResponse>(p.rig.settle_op(*p.client, *stop)));
    CHECK(p.winhost->stop_grace_ms().has_value());
}
#endif

TEST_CASE("play flow: a Wine runner is rated from the manifest the engine starts with") {
    // Windows-shaped otherwise, so this runs everywhere; only Support.Query asks about Wine.
    const ManifestSigner signer;
    const std::string archive = tar_of({{"bin/wine", "#!wine"}});
    const std::string manifest = manifest_text(kClientDll, runtime_entry(archive));
    EngineRig rig;
    auto runner = std::make_unique<testing::FakeRunnerPlatform>(std::vector{ports::RunnerKind::Wine});
    rig.platform.runner = std::move(runner);
    write_file(rig.install.bundled_manifest, manifest);
    NativePath signature = rig.install.bundled_manifest;
    signature += ".sig";
    write_file(signature, signer.signature_file(manifest));
    EngineOverrides overrides;
    overrides.manifest_keys = signer.keys();
    rig.boot(EngineOrigin::Foreground, std::move(overrides));
    auto client = rig.connect();

    api::SupportQueryRequest query;
    query.version = api::VersionRef{api::GameVersion{12, 41, std::nullopt}, kChangelist};
    query.role = api::GameRole::Client;
    query.runner = api::RunnerKind::Wine;
    const Result<api::SupportQueryResponse> rated = rig.call<api::SupportQueryResponse>(*client, api::kSupportQuery, query);
    REQUIRE(rated);
    CHECK(std::ranges::none_of(rated->reasons,
                               [](const api::Diagnostic& reason) { return reason.id == "support.runner_unavailable"; }));
    CHECK(rated->tier != api::SupportTier::Blocked);
}

TEST_CASE("play flow: without a selected build the plan names the blocker and the start is refused") {
    PlayRig p;
    const Result<api::PlayPlanResponse> plan =
        p.rig.call<api::PlayPlanResponse>(*p.client, api::kPlayPlan, api::PlayPlanRequest{api::PlayRequest{}});
    REQUIRE(plan);
    REQUIRE_FALSE(plan->blockers.empty());
    CHECK(plan->blockers.front().id == "play.no_build_selected");
    CHECK_FALSE(p.rig.start(*p.client, api::kPlayStart, api::PlayStartRequest{api::PlayRequest{}}));
}
