// CI gate: flows run with a known password and token, then no log file, ring, export or stored file holds them.
#include <openssl/evp.h>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <chrono>
#include <filesystem>
#include <memory>
#include <mutex>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "engine_rig.hpp"
#include "reboot/api/v1/host.hpp"
#include "reboot/api/v1/library.hpp"
#include "reboot/api/v1/logs.hpp"
#include "reboot/api/v1/play.hpp"
#include "reboot/api/v1/secrets.hpp"
#include "reboot/api/v1/sessions.hpp"
#include "reboot/contracts/game_client.hpp"
#include "reboot/foundation/clock.hpp"
#include "reboot/foundation/log.hpp"
#include "reboot/foundation/native_path.hpp"
#include "reboot/foundation/sha256.hpp"
#include "reboot/logging/file_log_sink.hpp"
#include "reboot/logging/log_files_in_use.hpp"
#include "reboot/logging/log_filter.hpp"
#include "reboot/logging/log_ring.hpp"
#include "reboot/logging/wine_log_files.hpp"
#include "reboot/testing/fake_client_dll.hpp"
#include "reboot/testing/fake_session_control.hpp"
#include "reboot/testing/in_memory_log_file_system.hpp"
#include "reboot/trust/key_ring.hpp"
#include "reboot/trust/signed_document.hpp"
#include "reboot/trust/signed_document_kind.hpp"

// Last: on Windows it brings in <windows.h> and its min and max macros.
#include <archive.h>
#include <archive_entry.h>

using namespace reboot;
using namespace reboot::engine;
using namespace reboot::engine::test;
using namespace std::chrono_literals;

namespace {

constexpr std::string_view kJoinPassword = "gate-join-password-7Hq2Lw";
constexpr std::string_view kNewJoinPassword = "gate-join-password-rotated-Pz4m";
constexpr std::string_view kBackendPassword = "gate-backend-password-Vx9cKe";
constexpr std::string_view kBackendHost = "backend.gate.test";
constexpr std::size_t kLoggerBudget = 16u << 20;

// Every record reaches the engine's ring and a session log file, as EngineHost wires them.
class GateSink final : public LogSink {
public:
    void attach(logging::LogRing& ring, std::unique_ptr<logging::FileLogSink> file) {
        const std::scoped_lock lock(mutex_);
        ring_ = &ring;
        file_ = std::move(file);
    }
    void detach() {
        const std::scoped_lock lock(mutex_);
        ring_ = nullptr;
        file_.reset();
    }

    void write(std::span<const LogRecord> records) override {
        const std::scoped_lock lock(mutex_);
        if (ring_ != nullptr) ring_->write(records);
        if (file_) file_->write(records);
    }
    void flush() override {
        const std::scoped_lock lock(mutex_);
        if (file_) file_->flush();
    }

private:
    std::mutex mutex_;
    logging::LogRing* ring_ = nullptr;
    std::unique_ptr<logging::FileLogSink> file_;
};

// The Logger is process-wide, so it is installed once, at the most verbose level.
GateSink& gate_sink() {
    static GateSink* const sink = [] {
        auto owned = std::make_unique<GateSink>();
        GateSink* raw = owned.get();
        Logger::add_sink(std::move(owned));
        Logger::set_level(LogLevel::Trace);
        Logger::install(kLoggerBudget);
        return raw;
    }();
    return *sink;
}

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

[[nodiscard]] api::Path api_path(const NativePath& native) {
    WirePath wire = to_wire(native);
    return api::Path{std::move(wire.display), std::move(wire.native)};
}

[[nodiscard]] std::span<const u8> bytes_of(std::string_view text) {
    return {reinterpret_cast<const u8*>(text.data()), text.size()};
}

// A name that carries a credential in an environment block.
[[nodiscard]] bool secret_name(std::string_view name) {
    return name.find("TOKEN") != std::string_view::npos || name.find("PASSWORD") != std::string_view::npos ||
           name.find("SECRET") != std::string_view::npos;
}

struct Haystack {
    std::string source;
    std::string text;
};

// Every entry of a zip, decompressed.
[[nodiscard]] std::vector<Haystack> zip_entries(const NativePath& zip) {
    std::vector<Haystack> out;
    const std::unique_ptr<archive, decltype(&archive_read_free)> reader(archive_read_new(), &archive_read_free);
    REQUIRE(reader);
    REQUIRE(archive_read_support_format_zip(reader.get()) == ARCHIVE_OK);
    const std::string raw = read_file(zip);
    REQUIRE(archive_read_open_memory(reader.get(), raw.data(), raw.size()) == ARCHIVE_OK);
    archive_entry* entry = nullptr;
    while (archive_read_next_header(reader.get(), &entry) == ARCHIVE_OK) {
        Haystack item{"export:" + std::string(archive_entry_pathname(entry)), {}};
        std::array<char, 16384> chunk{};
        for (la_ssize_t got = 0; (got = archive_read_data(reader.get(), chunk.data(), chunk.size())) > 0;)
            item.text.append(chunk.data(), static_cast<std::size_t>(got));
        out.push_back(std::move(item));
    }
    return out;
}

class SecretGate {
public:
    SecretGate() {
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
            dll = std::make_unique<testing::FakeClientDll>(rig.strand, rig.clock, std::move(script));
            REQUIRE(dll->connect(rig.io, *bootstrap));
        });

        GateSink& sink = gate_sink();
        Result<std::unique_ptr<logging::FileLogSink>> file = logging::FileLogSink::open(
            logging::FileLogOptions{.dir = rig.layout->logs_dir(),
                                    .group = {std::chrono::floor<std::chrono::seconds>(wall.system_now()), rig.info.self.pid},
                                    .role = "engine"},
            wall, files, in_use, nullptr);
        REQUIRE(file);
        sink.attach(rig.log_ring, std::move(*file));

        EngineOverrides overrides;
        overrides.manifest_keys = signer.keys();
        rig.boot(EngineOrigin::Foreground, std::move(overrides));
        client = rig.connect();
        rig.auto_accept(*client, {api::UserRequestKind::ConfirmUntested});
    }

    ~SecretGate() {
        // The DLL's socket callbacks run on the rig's I/O thread, so it goes before the rig stops.
        if (dll) dll->disconnect();
        Logger::flush();
        gate_sink().detach();
    }

    SecretGate(const SecretGate&) = delete;
    SecretGate& operator=(const SecretGate&) = delete;

    void put_secret(const api::SecretTarget& target, std::string_view secret) {
        client->secret_put(api::encode(target), bytes_of(secret));
        REQUIRE(rig.strand.pump_until([&] {
            const Result<api::SecretsStateResponse> state =
                rig.call<api::SecretsStateResponse>(*client, api::kSecretsState, api::SecretsStateRequest{target});
            return state && state->present;
        }));
    }

    void stop(const api::SessionId& session) {
        Result<u64> op = rig.start(*client, api::kSessionsStop, api::SessionsStopRequest{session, 0});
        REQUIRE(op);
        static_cast<void>(EngineRig::completed<api::SessionsStopResponse>(rig.settle_op(*client, *op)));
    }

    // Credentials the engine handed its children: tokens and passwords in their argv and environment.
    void harvest_children() {
        for (testing::ScriptedChild* child : rig.processes->children()) harvest(child->launch().args, child->launch().env);
        for (testing::FakeSessionControl* session : rig.session_host->sessions())
            harvest(session->launch().args, session->launch().env);
        // The rig's game server pointers die with their process, so they are read only while it runs.
        for (testing::FakeGameServer* server : rig.game_servers)
            if (const auto config = server->config(); config && config->backend)
                add("game server service token", config->backend->service_token);
        rig.game_servers.clear();
    }

    void add(std::string label, std::string_view value) {
        if (value.empty()) return;
        for (const auto& known : secrets)
            if (known.first == label && known.second == value) return;
        secrets.emplace_back(std::move(label), std::string(value));
    }

    [[nodiscard]] bool knows(std::string_view label) const {
        return std::ranges::any_of(secrets, [&](const auto& known) { return known.first == label; });
    }

    // The ring, the session log files, the export of them and every file under the data and cache roots.
    [[nodiscard]] std::vector<Haystack> haystacks() {
        Logger::flush();
        std::vector<Haystack> out;

        Haystack ring{"ring", {}};
        logging::LogCursor cursor;
        for (;;) {
            const logging::LogPage page = rig.log_ring.read(cursor, logging::LogFilter{}, 4096);
            for (const logging::LogEntry& entry : page.entries) ring.text += entry.record.text + "\n";
            if (page.entries.empty()) break;
            cursor = page.next;
        }
        REQUIRE_FALSE(ring.text.empty());
        out.push_back(std::move(ring));

        const NativePath logs = rig.layout->logs_dir();
        const std::vector<std::string> names = files.file_names(logs);
        REQUIRE_FALSE(names.empty());
        for (const std::string& name : names) {
            std::optional<std::string> text = files.text(logs / name);
            REQUIRE(text);
            // The export reads the logs directory on disk.
            write_file(logs / name, *text);
            out.push_back({"log:" + name, std::move(*text)});
        }

        const NativePath zip = rig.scratch->path() / "gate-export.zip";
        api::LogsExportRequest request;
        request.destination = api_path(zip);
        Result<u64> op = rig.start(*client, api::kLogsExport, request);
        REQUIRE(op);
        static_cast<void>(EngineRig::completed<api::LogsExportResponse>(rig.settle_op(*client, *op)));
        out.push_back({"export (raw)", read_file(zip)});
        std::vector<Haystack> entries = zip_entries(zip);
        REQUIRE(entries.size() > names.size());
        for (Haystack& entry : entries) out.push_back(std::move(entry));

        for (const NativePath& root : {rig.layout->root(), rig.paths->default_cache_root()}) {
            std::error_code error;
            for (auto it = std::filesystem::recursive_directory_iterator(root, error);
                 !error && it != std::filesystem::recursive_directory_iterator(); it.increment(error))
                if (it->is_regular_file()) out.push_back({"file:" + display_utf8(it->path()), read_file(it->path())});
        }
        return out;
    }

    ManifestSigner signer;
    EngineRig rig;
    SystemClock wall;
    testing::InMemoryLogFileSystem files{wall};
    logging::LogFilesInUse in_use;
    NativePath build_root;
    std::unique_ptr<testing::FakeClientDll> dll;
    std::unique_ptr<testing::ApiTestClient> client;
    // Label and value.
    std::vector<std::pair<std::string, std::string>> secrets;

private:
    void harvest(const std::vector<std::string>& args, const ports::EnvBlock& env) {
        constexpr std::string_view kAuthPassword = "-AUTH_PASSWORD=";
        for (const std::string& arg : args)
            if (arg.starts_with(kAuthPassword)) add("game -AUTH_PASSWORD", std::string_view(arg).substr(kAuthPassword.size()));
        for (const auto& [name, value] : env.vars)
            if (secret_name(name)) add("environment " + name, value);
    }
};

}  // namespace

TEST_CASE("secret grep: known passwords and tokens reach no log, ring, export or stored file", "[gate]") {
    SecretGate g;
    g.add("backend password", kBackendPassword);
    g.add("join password", kJoinPassword);
    g.add("rotated join password", kNewJoinPassword);

    api::SecretTarget backend_target;
    backend_target.kind = api::SecretKind::BackendPassword;
    backend_target.backend = api::BackendHost{std::string(kBackendHost), std::nullopt};
    g.put_secret(backend_target, kBackendPassword);

    // Host with a join password, rotated while the server runs.
    Result<api::HostProfilesCreateResponse> created = g.rig.call<api::HostProfilesCreateResponse>(
        *g.client, api::kHostProfilesCreate, api::HostProfilesCreateRequest{EngineRig::arena_profile()});
    REQUIRE(created);
    api::SecretTarget join_target;
    join_target.kind = api::SecretKind::HostJoinPassword;
    join_target.host_profile = created->profile.id;
    g.put_secret(join_target, kJoinPassword);
    api::HostStartRequest host;
    host.profile = created->profile.id;
    Result<u64> hosting = g.rig.start(*g.client, api::kHostStart, host);
    REQUIRE(hosting);
    const api::SessionId hosted = EngineRig::completed<api::HostStartResponse>(g.rig.settle_op(*g.client, *hosting)).session;
    g.put_secret(join_target, kNewJoinPassword);
    g.harvest_children();
    g.stop(hosted);

    // Play an imported build on the embedded backend, with our DLL and its control token.
    api::LibraryImportRequest import_request;
    import_request.path = api_path(g.build_root);
    import_request.name = "Chapter 2";
    import_request.version = api::GameVersion{12, 41, std::nullopt};
    import_request.changelist = kChangelist;
    Result<u64> importing = g.rig.start(*g.client, api::kLibraryImport, import_request);
    REQUIRE(importing);
    const api::Build build = EngineRig::completed<api::LibraryImportResponse>(g.rig.settle_op(*g.client, *importing)).build;
    REQUIRE(g.rig.call<api::LibrarySelectResponse>(*g.client, api::kLibrarySelect,
                                                   api::LibrarySelectRequest{api::GameRole::Client, build.id}));
    Result<u64> playing = g.rig.start(*g.client, api::kPlayStart, api::PlayStartRequest{api::PlayRequest{}});
    REQUIRE(playing);
    const api::SessionId played = EngineRig::completed<api::PlayStartResponse>(g.rig.settle_op(*g.client, *playing)).session;
    g.harvest_children();
    g.stop(played);

    // Without these the grep would prove nothing about the flows' tokens.
    REQUIRE(g.knows("game server service token"));
    REQUIRE(g.knows("game -AUTH_PASSWORD"));
    REQUIRE(g.knows("environment " + std::string(contracts::game_client::kEnvCtlToken)));

    for (const Haystack& haystack : g.haystacks())
        for (const auto& [label, value] : g.secrets) {
            INFO(label << " in " << haystack.source);
            CHECK(haystack.text.find(value) == std::string::npos);
        }
}
