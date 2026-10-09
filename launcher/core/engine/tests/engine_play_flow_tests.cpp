#include <openssl/evp.h>

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "api_convert.hpp"
#include "engine_rig.hpp"
#include "reboot/api/v1/components.hpp"
#include "reboot/api/v1/engine.hpp"
#include "reboot/api/v1/library.hpp"
#include "reboot/api/v1/play.hpp"
#include "reboot/api/v1/sessions.hpp"
#include "reboot/contracts/game_client.hpp"
#include "reboot/foundation/sha256.hpp"
#include "reboot/testing/fake_client_dll.hpp"
#include "reboot/testing/fake_session_control.hpp"
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

[[nodiscard]] std::string sha_hex(std::string_view content) {
    return to_hex(sha256(std::span(reinterpret_cast<const u8*>(content.data()), content.size())));
}

[[nodiscard]] std::string remote_file(std::string_view role, std::string_view name, std::string_view content) {
    return R"({"role":")" + std::string(role) + R"(","urls":["https://cdn.test/payload/)" + std::string(name) +
           R"("],"sha256":")" + sha_hex(content) + R"(","size":)" + std::to_string(content.size()) + "}";
}

// One payload, valid long after the rig's wall clock.
[[nodiscard]] std::string manifest_text() {
    return R"({"schema":1,"serial":3,"expires_unix_ms":4000000000000,"apps":[],"payloads":[{"version":"1.0.0","payload_abi":)" +
           std::to_string(contracts::game_client::kPayloadAbi) + R"(,"files":[)" +
           remote_file("client_dll", "rb_client.dll", kClientDll) + "," + remote_file("winhost", "reboot-winhost.exe", kWinhost) +
           R"(]}],"runtimes":[]})";
}

[[nodiscard]] testing::FakeHttpResponse body_of(std::string_view content) {
    testing::FakeHttpResponse response;
    response.body.assign(content.begin(), content.end());
    return response;
}

struct PlayRig {
    PlayRig() {
        const std::string manifest = manifest_text();
        write_file(rig.install.bundled_manifest, manifest);
        NativePath signature = rig.install.bundled_manifest;
        signature += ".sig";
        write_file(signature, signer.signature_file(manifest));
        rig.http.route("GET", "https://cdn.test/payload/rb_client.dll", body_of(kClientDll));
        rig.http.route("GET", "https://cdn.test/payload/reboot-winhost.exe", body_of(kWinhost));

        build_root = rig.scratch->path() / "builds" / "12.41";
        write_file(build_root / "FortniteGame" / "Binaries" / "Win64" / "FortniteClient-Win64-Shipping.exe", "MZ game");

        rig.serve_backend();
        rig.serve_game_server();
        rig.session_host->on_resume([this](testing::FakeSessionControl& control) {
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

    // A second play session is refused while one runs.
    const Result<u64> second = p.rig.start(*p.client, api::kPlayStart, api::PlayStartRequest{api::PlayRequest{}});
    if (second) CHECK(p.rig.settle_op(*p.client, *second).failed);

    Result<u64> stop = p.rig.start(*p.client, api::kSessionsStop, api::SessionsStopRequest{started.session, 0});
    REQUIRE(stop);
    static_cast<void>(EngineRig::completed<api::SessionsStopResponse>(p.rig.settle_op(*p.client, *stop)));
    CHECK(p.rig.session_host->last()->stop_grace().has_value());
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

TEST_CASE("play flow: without a selected build the plan names the blocker and the start is refused") {
    PlayRig p;
    const Result<api::PlayPlanResponse> plan =
        p.rig.call<api::PlayPlanResponse>(*p.client, api::kPlayPlan, api::PlayPlanRequest{api::PlayRequest{}});
    REQUIRE(plan);
    REQUIRE_FALSE(plan->blockers.empty());
    CHECK(plan->blockers.front().id == "play.no_build_selected");
    CHECK_FALSE(p.rig.start(*p.client, api::kPlayStart, api::PlayStartRequest{api::PlayRequest{}}));
}
