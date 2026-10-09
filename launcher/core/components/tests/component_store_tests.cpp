#include <any>
#include <catch2/catch_test_macros.hpp>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "components_test_support.hpp"
#include "test_archives.hpp"
#include "reboot/components/component_changed_event.hpp"
#include "reboot/components/component_store.hpp"
#include "reboot/components/manifest_service.hpp"
#include "reboot/foundation/events.hpp"
#include "reboot/foundation/operation.hpp"
#include "reboot/foundation/paths.hpp"
#include "reboot/foundation/random.hpp"
#include "reboot/net/host_tls_memory.hpp"
#include "reboot/net/http_client.hpp"
#include "reboot/net/resumable_downloader.hpp"
#include "reboot/testing/fake_disk_info.hpp"
#include "reboot/testing/fake_file_watcher.hpp"
#include "reboot/testing/fake_http_transport.hpp"
#include "reboot/testing/fake_platform_paths.hpp"
#include "reboot/testing/fake_random.hpp"
#include "reboot/testing/fake_security_probe.hpp"
#include "reboot/testing/in_memory_file_system.hpp"
#include "reboot/testing/scratch_dir.hpp"
#include "reboot/trust/serial_guard.hpp"

using namespace rb;
using namespace rb::components;
using namespace std::chrono_literals;
using rb::components::test::arg_text;
using rb::components::test::bytes_of;
using rb::components::test::DiskFileSystem;
using rb::components::test::make_tar_gz;
using rb::components::test::ManifestJson;
using rb::components::test::read_text;
using rb::components::test::sha_hex;
using rb::components::test::TestSigner;
using rb::components::test::TestStrand;
using rb::components::test::write_text;
using rb::testing::FakeHttpResponse;
using rb::testing::FsOperation;

namespace {

constexpr std::string_view kManifestUrl = "https://cdn.test/manifest.json";
constexpr std::string_view kManifestSigUrl = "https://cdn.test/manifest.json.sig";
const ManifestPlatform kLinux{ManifestOs::Linux, ManifestArch::X64};
const ManifestPlatform kWindows{ManifestOs::Windows, ManifestArch::X64};

const std::string kClientV1 = "client dll, version 1";
const std::string kClientV2 = "client dll, version 2";
const std::string kWinhost = "winhost helper";

std::string client_url(std::string_view version) { return "https://cdn.test/" + std::string(version) + "/rb_client.dll"; }
std::string winhost_url(std::string_view version) {
    return "https://cdn.test/" + std::string(version) + "/reboot-winhost.exe";
}

// Built on first use, inside a test, since building it may fail a REQUIRE.
const std::string& wine_archive() {
    static const std::string archive = make_tar_gz({{"bin/wine", "#!wine"}, {"lib/wine/ntdll.so", "ntdll"}});
    return archive;
}

// OS randomness: ctest runs test cases of this binary in parallel processes, and one fixed seed
// would give them all the same directory names.
testing::ScratchDir make_scratch() {
    OsRandom random;
    Result<testing::ScratchDir> dir = testing::ScratchDir::create(random, "reboot-components");
    REQUIRE(dir);
    return std::move(*dir);
}

// A path being deleted reads as access denied on Windows until its last handle closes.
bool gone(const NativePath& path) {
    std::error_code error;
    return !std::filesystem::exists(path, error);
}

FakeHttpResponse ok_body(std::string_view text) {
    FakeHttpResponse response;
    response.body = bytes_of(text);
    return response;
}

FakeHttpResponse status(u32 code) {
    FakeHttpResponse response;
    response.status = code;
    return response;
}

ManifestJson payload_manifest(u64 serial = 1) {
    ManifestJson json;
    json.serial = serial;
    json.payload("1.0.0", VersionStreams::payload_abi, kClientV1, kWinhost);
    json.runtime("kron-10", "kron_wine", "linux", "https://cdn.test/kron-10.tar.gz", wine_archive());
    return json;
}

struct Fixture {
    explicit Fixture(NativePath root, ManifestPlatform platform = kLinux) : root_dir(std::move(root)) {
        volumes.add_volume(ports::VolumeInfo{.mount = this->root(), .free_bytes = 1u << 30, .total_bytes = 1u << 31});
        options.platform = platform;
    }
    explicit Fixture(ManifestPlatform platform = kLinux) : Fixture(NativePath(), platform) {}

    ~Fixture() {
        // Queued worker jobs still run and post to a strand nobody runs any more.
        workers.shutdown();
    }

    // The bundled manifest the manifest service starts from, with its artifacts served.
    void publish(const ManifestJson& json) {
        const std::string body = json.text();
        manifest_fs.write_text(install.bundled_manifest, body);
        manifest_fs.write_text(NativePath(install.bundled_manifest) += ".sig", signer.signature_file(body));
        serve_artifacts();
    }

    void serve_artifacts() {
        for (const std::string& version : {std::string("1.0.0"), std::string("2.0.0")}) {
            transport.route("GET", client_url(version), ok_body(version == "1.0.0" ? kClientV1 : kClientV2));
            transport.route("GET", winhost_url(version), ok_body(kWinhost));
        }
        transport.route("GET", "https://cdn.test/kron-10.tar.gz", ok_body(wine_archive()));
    }

    // A newer manifest fetched by refresh, which runs the store's listener.
    void refresh_to(const ManifestJson& json) {
        const std::string body = json.text();
        transport.route("GET", std::string(kManifestUrl), ok_body(body));
        transport.route("GET", std::string(kManifestSigUrl), ok_body(signer.signature_file(body)));
        std::optional<Result<ManifestRefresh>> refreshed;
        manifest->refresh({}, [&](Result<ManifestRefresh> r) { refreshed = std::move(r); });
        strand.run_until([&] { return refreshed.has_value(); });
        REQUIRE(*refreshed == ManifestRefresh::Adopted);
    }

    void start() {
        create();
        std::optional<Result<void>> loaded;
        store->load([&](Result<void> result) { loaded = std::move(result); });
        strand.run_until([&] { return loaded.has_value(); });
        REQUIRE(*loaded);
    }

    // The manifest service, loaded, and a store that is not.
    void create() {
        manifest = std::make_unique<ManifestService>(
            ManifestServiceDeps{manifest_fs, http, workers, strand, clock, keys, serials}, options, layout, install);
        std::optional<Result<ManifestOrigin>> origin;
        manifest->load([&](Result<ManifestOrigin> loaded) { origin = std::move(loaded); });
        strand.run_until([&] { return origin.has_value(); });
        REQUIRE(*origin);

        store = std::make_unique<ComponentStore>(
            ComponentStoreDeps{disk, watcher, security, downloader, workers, strand, ops, events, *manifest}, layout);
    }

    Result<PinnedPayload> acquire_payload(CancelToken token = {}) {
        std::optional<Result<PinnedPayload>> result;
        store->acquire_payload(SessionId{}, std::move(token), [this](const Progress& p) { phases.emplace_back(p.phase); },
                               [&](Result<PinnedPayload> r) { result = std::move(r); });
        strand.run_until([&] { return result.has_value(); });
        return std::move(*result);
    }

    Result<PinnedRuntime> acquire_runtime(std::string_view id) {
        std::optional<Result<PinnedRuntime>> result;
        store->acquire_runtime(SessionId{}, id, {}, [this](const Progress& p) { phases.emplace_back(p.phase); },
                               [&](Result<PinnedRuntime> r) { result = std::move(r); });
        strand.run_until([&] { return result.has_value(); });
        return std::move(*result);
    }

    Result<IntegrityHold> hold(const PayloadSet& set, PayloadRole role) {
        std::optional<Result<IntegrityHold>> result;
        store->hold(set, role, {}, nullptr, [&](Result<IntegrityHold> r) { result = std::move(r); });
        strand.run_until([&] { return result.has_value(); });
        return std::move(*result);
    }

    ErasedOutcome run_op(OpHandle handle) {
        strand.run_until([&] { return ops.outcome(handle.id()).has_value(); });
        return *ops.outcome(handle.id());
    }

    // The newest event per component id since the last call.
    std::vector<ComponentChangedEvent> drain() {
        std::vector<EventEnvelope> raw;
        subscription->drain(raw, 1000);
        std::vector<ComponentChangedEvent> out;
        for (const EventEnvelope& event : raw) out.push_back(std::any_cast<ComponentChangedEvent>(event.payload));
        seen.insert(seen.end(), out.begin(), out.end());
        return out;
    }

    // Runs the strand until an event satisfies `match`.
    template <class Match>
    ComponentChangedEvent wait_event(Match&& match) {
        std::optional<ComponentChangedEvent> found;
        strand.run_until([&] {
            for (ComponentChangedEvent& event : drain())
                if (match(event)) found = std::move(event);
            return found.has_value();
        });
        return std::move(*found);
    }

    // Runs the strand until the single worker is idle and nothing new reached it. A file the store
    // writes is read only then: a read that overlaps the write's rename makes that write fail.
    void settle() {
        while (true) {
            const u64 before = strand.executed();
            bool flushed = false;
            workers.submit<void>([](CancelToken) -> Result<void> { return {}; }, {}, strand,
                                 [&](Result<void>) { flushed = true; });
            strand.run_until([&] { return flushed; });
            if (strand.executed() - before == 1) return;
        }
    }

    [[nodiscard]] std::optional<ComponentInfo> info(std::string_view id, std::string_view version) const {
        for (const ComponentInfo& item : store->list())
            if (item.ref.id == id && item.ref.version == version) return item;
        return std::nullopt;
    }

    [[nodiscard]] NativePath payload_file(std::string_view content, PayloadRole role) const {
        return layout.components_dir() / "payload" / sha_hex(content) / std::string(payload_file_name(role));
    }

    [[nodiscard]] std::size_t requests_to(std::string_view url) const {
        std::size_t count = 0;
        for (const ports::HttpRequest& request : transport.requests())
            if (request.url == url) ++count;
        return count;
    }

    // A test that restarts the store passes a directory that outlives both fixtures.
    NativePath root_dir;
    std::optional<testing::ScratchDir> own_dir = root_dir.empty() ? std::optional(make_scratch()) : std::nullopt;

    ManualClock clock;
    TestStrand strand{clock};
    TimerService timers{clock, strand};
    testing::FakeHttpTransport transport{strand, clock};
    testing::FakeRandom random{5};
    net::HostTlsMemory tls{{}, nullptr};
    net::HttpClient http{transport, tls, strand, timers, random};
    DiskFileSystem disk;
    testing::FakeDiskInfo volumes;
    // One worker keeps the order of blocking jobs, and so of their results, fixed.
    WorkerPool workers{1};
    net::ResumableDownloader downloader{http, volumes, disk, workers, strand, timers, clock};
    testing::FakeFileWatcher watcher{strand};
    testing::FakeSecurityProbe security;
    EventBus events{EngineEpoch{1}};
    OpRegistry ops{clock, timers, events};
    std::shared_ptr<Subscription> subscription = events.subscribe(EventFilter{{EventKind::ComponentChanged}, {}, {}}, 1u << 22);

    testing::InMemoryFileSystem manifest_fs;
    TestSigner signer;
    trust::KeyRing keys = signer.ring();
    trust::SerialGuard serials{trust::SignedDocumentKind::ReleaseManifest, 0, [](u64) { return Result<void>{}; }};
    testing::FakePlatformPaths paths{root()};
    AppLayout layout{DataRoot{root() / "data", true}, paths};
    InstallLayout install{.install_dir = "/app",
                          .backend_exe = "/app/reboot-backend",
                          .game_server_exe = "/app/reboot-game-server",
                          .backend_content_dir = "/app/backend-content",
                          .bundled_catalog = "/app/catalog.json",
                          .bundled_manifest = "/app/manifest.json"};
    ManifestOptions options{std::string(kManifestUrl), std::string(kManifestSigUrl), "stable", kLinux};

    std::vector<std::string> phases;
    std::vector<ComponentChangedEvent> seen;
    std::unique_ptr<ManifestService> manifest;
    std::unique_ptr<ComponentStore> store;

private:
    [[nodiscard]] NativePath root() const { return own_dir ? own_dir->path() : root_dir; }
};

}  // namespace

TEST_CASE("a fresh store lists what the manifest selects as missing", "[components][store]") {
    Fixture f;
    f.publish(payload_manifest());
    f.start();
    CHECK(std::filesystem::is_directory(f.layout.components_dir() / ".staging"));
    const auto payload = f.info("payload", "1.0.0");
    REQUIRE(payload);
    CHECK(payload->state == ComponentState::Missing);
    CHECK(payload->selected);
    CHECK(payload->size_bytes == kClientV1.size() + kWinhost.size());
    const auto runtime = f.info("kron-10", "1");
    REQUIRE(runtime);
    CHECK(runtime->state == ComponentState::Missing);
    CHECK(runtime->ref.kind == ComponentKind::Runtime);
}

TEST_CASE("ensure fetches the selected payload into the content-addressed store", "[components][store]") {
    Fixture f;
    f.publish(payload_manifest());
    f.start();
    auto handle = f.store->start_ensure("payload", DisconnectPolicy::Detached);
    REQUIRE(handle);
    const ErasedOutcome outcome = f.run_op(*handle);
    const auto* completed = std::get_if<Completed<std::any>>(&outcome);
    REQUIRE(completed != nullptr);
    CHECK(std::any_cast<ComponentRef>(completed->value) == ComponentRef{ComponentKind::Payload, "payload", "1.0.0"});

    CHECK(read_text(f.payload_file(kClientV1, PayloadRole::ClientDll)) == kClientV1);
    CHECK(read_text(f.payload_file(kWinhost, PayloadRole::Winhost)) == kWinhost);
    const auto info = f.info("payload", "1.0.0");
    REQUIRE(info);
    CHECK(info->state == ComponentState::Ready);
    CHECK(info->pins == 0);

    const NativePath index = f.layout.components_dir() / "index.json";
    f.settle();
    CHECK(read_text(index).find("1.0.0") != std::string::npos);
    CHECK(read_text(index).find(sha_hex(kWinhost)) != std::string::npos);
    CHECK(f.wait_event([](const ComponentChangedEvent& e) { return e.info.state == ComponentState::Ready; })
              .info.ref.version == "1.0.0");
}

TEST_CASE("ensure validates synchronously and creates no op for a bad id", "[components][store]") {
    Fixture f;
    f.publish(payload_manifest());
    f.start();
    const auto unknown = f.store->start_ensure("nope", DisconnectPolicy::Detached);
    REQUIRE_FALSE(unknown);
    CHECK(unknown.error().id == "components.unknown_runtime");
    const auto removed = f.store->start_remove("nope", DisconnectPolicy::Detached);
    REQUIRE_FALSE(removed);
    CHECK(removed.error().id == "components.unknown_component");
    CHECK(f.ops.live().empty());
}

TEST_CASE("acquire pins the verified payload and remove refuses while it is pinned", "[components][store]") {
    Fixture f;
    f.publish(payload_manifest());
    f.start();
    auto pinned = f.acquire_payload();
    REQUIRE(pinned);
    CHECK(pinned->pin.held());
    CHECK(pinned->set.ref.version == "1.0.0");
    REQUIRE(pinned->set.files.size() == 2);
    const StoredFile* winhost = pinned->set.find(PayloadRole::Winhost);
    REQUIRE(winhost != nullptr);
    CHECK(winhost->path == f.payload_file(kWinhost, PayloadRole::Winhost));
    CHECK(to_hex(winhost->sha256) == sha_hex(kWinhost));
    CHECK(f.info("payload", "1.0.0")->pins == 1);
    CHECK(std::ranges::find(f.phases, std::string("download")) != f.phases.end());

    const auto refused = f.store->start_remove("payload", DisconnectPolicy::Detached);
    REQUIRE_FALSE(refused);
    CHECK(refused.error().id == "components.pinned");

    // A second acquire re-hashes the stored files without downloading them again.
    const std::size_t requests = f.transport.requests().size();
    auto again = f.acquire_payload();
    REQUIRE(again);
    CHECK(f.transport.requests().size() == requests);
    CHECK(f.info("payload", "1.0.0")->pins == 2);

    pinned->pin.release();
    again->pin.release();
    CHECK(f.info("payload", "1.0.0")->pins == 0);
    auto handle = f.store->start_remove("payload", DisconnectPolicy::Detached);
    REQUIRE(handle);
    CHECK(std::holds_alternative<Completed<std::any>>(f.run_op(*handle)));
    CHECK(gone(f.payload_file(kClientV1, PayloadRole::ClientDll)));
    CHECK(f.info("payload", "1.0.0")->state == ComponentState::Missing);
}

TEST_CASE("Windows stores and verifies only the client DLL", "[components][store]") {
    Fixture f(kWindows);
    f.publish(payload_manifest());
    f.start();
    auto pinned = f.acquire_payload();
    REQUIRE(pinned);
    REQUIRE(pinned->set.files.size() == 1);
    CHECK(pinned->set.files[0].role == PayloadRole::ClientDll);
    CHECK(f.requests_to(winhost_url("1.0.0")) == 0);
    CHECK(gone(f.payload_file(kWinhost, PayloadRole::Winhost)));
}

TEST_CASE("a mirror with the wrong bytes is skipped for the next url", "[components][store]") {
    ManifestJson json;
    json.payloads.push_back(R"({"version":"1.0.0","payload_abi":)" + std::to_string(VersionStreams::payload_abi) +
                            R"(,"files":[{"role":"client_dll","urls":["https://bad.test/rb_client.dll","https://cdn.test/1.0.0/rb_client.dll"],"sha256":")" +
                            sha_hex(kClientV1) + R"(","size":)" + std::to_string(kClientV1.size()) + "}]}");
    SECTION("the second mirror serves the right bytes") {
        Fixture f(kWindows);
        f.publish(json);
        f.transport.route("GET", "https://bad.test/rb_client.dll", ok_body(std::string(kClientV1.size(), 'x')));
        f.start();
        auto pinned = f.acquire_payload();
        REQUIRE(pinned);
        CHECK(read_text(pinned->set.files[0].path) == kClientV1);
    }
    SECTION("every mirror fails") {
        Fixture f(kWindows);
        f.publish(json);
        f.transport.route("GET", "https://bad.test/rb_client.dll", ok_body(std::string(kClientV1.size(), 'x')));
        f.transport.route("GET", client_url("1.0.0"), status(404));
        f.start();
        const auto pinned = f.acquire_payload();
        REQUIRE_FALSE(pinned);
        CHECK(pinned.error().id == "components.download_failed");
        CHECK(arg_text(pinned.error(), "component") == "payload");
        REQUIRE(pinned.error().causes.size() == 1);
        CHECK(pinned.error().causes[0].id.starts_with("net."));
        CHECK(f.info("payload", "1.0.0")->state == ComponentState::Missing);
        CHECK(f.requests_to("https://bad.test/rb_client.dll") == 1);
    }
}

TEST_CASE("a size the mirror contradicts fails the download", "[components][store]") {
    Fixture f(kWindows);
    f.publish(payload_manifest());
    f.transport.route("GET", client_url("1.0.0"), ok_body(kClientV1 + "trailing"));
    f.start();
    const auto pinned = f.acquire_payload();
    REQUIRE_FALSE(pinned);
    CHECK(pinned.error().id == "components.download_failed");
}

TEST_CASE("a stored file that changed is reported, re-fetched and recovered", "[components][store]") {
    Fixture f;
    f.publish(payload_manifest());
    f.start();
    REQUIRE(f.acquire_payload());
    f.drain();

    SECTION("the client DLL") {
        write_text(f.payload_file(kClientV1, PayloadRole::ClientDll), "tampered");
        auto pinned = f.acquire_payload();
        REQUIRE(pinned);
        CHECK(read_text(f.payload_file(kClientV1, PayloadRole::ClientDll)) == kClientV1);
        const auto event = f.wait_event([](const ComponentChangedEvent& e) {
            return e.problem && e.problem->recovery == RecoveryState::Recovered;
        });
        CHECK(event.problem->kind == ComponentProblemKind::FileVanishedAfterVerify);
        CHECK(event.problem->file == f.payload_file(kClientV1, PayloadRole::ClientDll));
        // A changed file is not a missing one, so the security probe was not asked.
        CHECK_FALSE(event.problem->security);
        CHECK(event.info.state == ComponentState::Ready);
        CHECK(f.requests_to(client_url("1.0.0")) == 2);
    }
    SECTION("winhost, attributed to the named security product") {
        f.security.set(std::optional(ports::SecurityProducts{{"Defender"}, ports::SmartAppControl::Off}));
        std::filesystem::remove(f.payload_file(kWinhost, PayloadRole::Winhost));
        REQUIRE(f.acquire_payload());
        const auto event = f.wait_event([](const ComponentChangedEvent& e) {
            return e.problem && e.problem->security && e.problem->recovery == RecoveryState::Recovered;
        });
        CHECK(event.problem->kind == ComponentProblemKind::HelperQuarantined);
        CHECK(event.problem->security->names == std::vector<std::string>{"Defender"});
        CHECK_FALSE(event.problem->remediation.empty());
        CHECK(to_diagnostic(*event.problem).id == "components.helper_quarantined");
    }
}

TEST_CASE("a file that vanishes right after its download is reported and fetched once more", "[components][store]") {
    Fixture f(kWindows);
    f.publish(payload_manifest());
    f.start();
    const Diagnostic gone = DiskFileSystem::error(f.layout.components_dir(), ErrorKind::NotFound);

    SECTION("the second download sticks") {
        // The check of the absent stored copy, then the hash of the first staged download.
        f.disk.faults().fail_next(FsOperation::OpenDenyWrite, gone, 2);
        auto pinned = f.acquire_payload();
        REQUIRE(pinned);
        const auto event = f.wait_event([](const ComponentChangedEvent& e) {
            return e.problem && e.problem->recovery == RecoveryState::Recovered;
        });
        CHECK(event.problem->kind == ComponentProblemKind::VanishedAfterDownload);
        CHECK(f.requests_to(client_url("1.0.0")) == 2);
    }
    SECTION("it vanishes again") {
        f.disk.faults().fail_next(FsOperation::OpenDenyWrite, gone, 3);
        const auto pinned = f.acquire_payload();
        REQUIRE_FALSE(pinned);
        CHECK(pinned.error().id == "components.download_vanished");
        const auto event = f.wait_event([](const ComponentChangedEvent& e) {
            return e.problem && e.problem->recovery == RecoveryState::RefetchFailed;
        });
        CHECK(event.problem->refetch_error);
        CHECK(f.info("payload", "1.0.0")->state == ComponentState::Missing);
    }
}

TEST_CASE("hold keeps the verified file open for injection", "[components][store]") {
    Fixture f;
    f.publish(payload_manifest());
    f.start();
    auto pinned = f.acquire_payload();
    REQUIRE(pinned);

    SECTION("a good file") {
        auto held = f.hold(pinned->set, PayloadRole::ClientDll);
        REQUIRE(held);
        CHECK(held->held());
        CHECK(to_hex(held->sha256()) == sha_hex(kClientV1));
    }
    SECTION("a file in use is neither blamed nor re-fetched") {
        f.disk.faults().fail_next(FsOperation::OpenDenyWrite, DiskFileSystem::error({}, ErrorKind::Conflict));
        const auto held = f.hold(pinned->set, PayloadRole::ClientDll);
        REQUIRE_FALSE(held);
        CHECK(held.error().id == "components.file_in_use");
        CHECK(f.requests_to(client_url("1.0.0")) == 1);
        f.drain();
        for (const ComponentChangedEvent& event : f.seen) CHECK_FALSE(event.problem);
    }
    SECTION("a changed file is re-fetched and held on the retry") {
        write_text(pinned->set.find(PayloadRole::ClientDll)->path, "tampered");
        auto held = f.hold(pinned->set, PayloadRole::ClientDll);
        REQUIRE(held);
        CHECK(to_hex(held->sha256()) == sha_hex(kClientV1));
        CHECK(f.requests_to(client_url("1.0.0")) == 2);
    }
    SECTION("a failed re-fetch fails the hold with both reasons") {
        write_text(pinned->set.find(PayloadRole::Winhost)->path, "tampered");
        f.transport.route("GET", winhost_url("1.0.0"), status(404));
        const auto held = f.hold(pinned->set, PayloadRole::Winhost);
        REQUIRE_FALSE(held);
        CHECK(held.error().id == "components.held_file_changed");
        REQUIRE_FALSE(held.error().causes.empty());
        CHECK(held.error().causes.back().id == "components.download_failed");
        const auto event = f.wait_event([](const ComponentChangedEvent& e) {
            return e.problem && e.problem->recovery == RecoveryState::RefetchFailed;
        });
        CHECK(event.problem->kind == ComponentProblemKind::HelperQuarantined);
        CHECK(f.info("payload", "1.0.0")->state == ComponentState::Broken);
    }
}

TEST_CASE("a deletion guard report is re-verified before anything is raised", "[components][store]") {
    Fixture f;
    f.publish(payload_manifest());
    f.start();
    REQUIRE(f.acquire_payload());
    const NativePath client = f.payload_file(kClientV1, PayloadRole::ClientDll);
    f.drain();

    SECTION("an intact file raises nothing") {
        f.watcher.emit(ports::FileChange{client, ports::FileChangeKind::Modified});
        f.strand.run_ready();
        // The single worker runs the re-check before this acquire's own checks.
        REQUIRE(f.acquire_payload());
        f.drain();
        for (const ComponentChangedEvent& event : f.seen) CHECK_FALSE(event.problem);
        CHECK(f.requests_to(client_url("1.0.0")) == 1);
    }
    SECTION("a removed file is reported and restored") {
        std::filesystem::remove(client);
        f.watcher.emit(ports::FileChange{client, ports::FileChangeKind::Removed});
        const auto event = f.wait_event([](const ComponentChangedEvent& e) {
            return e.problem && e.problem->recovery == RecoveryState::Recovered;
        });
        CHECK(event.problem->kind == ComponentProblemKind::FileVanishedAfterVerify);
        CHECK(read_text(client) == kClientV1);
    }
}

TEST_CASE("cancelling the only caller stops the fetch; a joined caller still gets its result", "[components][store]") {
    Fixture f(kWindows);
    ManifestJson json = payload_manifest();
    f.publish(json);
    FakeHttpResponse slow = ok_body(kClientV1);
    slow.delay = 5s;
    f.transport.route("GET", client_url("1.0.0"), slow);
    f.start();

    CancelSource first;
    std::optional<Result<PinnedPayload>> first_result;
    std::optional<Result<PinnedPayload>> second_result;
    f.store->acquire_payload(SessionId{}, first.token(), nullptr, [&](Result<PinnedPayload> r) { first_result = std::move(r); });

    SECTION("a joined caller survives the first one's cancel") {
        f.store->acquire_payload(SessionId{}, {}, nullptr, [&](Result<PinnedPayload> r) { second_result = std::move(r); });
        f.strand.run_until([&] { return f.transport.in_flight() == 1; });
        first.cancel(CancelReason::User);
        f.strand.run_until([&] { return first_result.has_value(); });
        REQUIRE_FALSE(*first_result);
        CHECK(first_result->error().kind == ErrorKind::Cancelled);
        f.strand.advance(5s);
        f.strand.run_until([&] { return second_result.has_value(); });
        REQUIRE(*second_result);
        CHECK(f.requests_to(client_url("1.0.0")) == 1);
    }
    SECTION("a caller joining while the cancelled fetch winds down gets a fresh one") {
        f.strand.run_until([&] { return f.transport.in_flight() == 1; });
        first.cancel(CancelReason::User);
        f.strand.run_until([&] { return first_result.has_value(); });
        f.store->acquire_payload(SessionId{}, {}, nullptr, [&](Result<PinnedPayload> r) { second_result = std::move(r); });
        f.strand.run_until([&] { return f.transport.in_flight() == 1 && f.requests_to(client_url("1.0.0")) == 2; });
        f.strand.advance(5s);
        f.strand.run_until([&] { return second_result.has_value(); });
        REQUIRE(*second_result);
        CHECK(f.info("payload", "1.0.0")->pins == 1);
    }
}

TEST_CASE("cancelling an ensure op cancels its download", "[components][store]") {
    Fixture f(kWindows);
    f.publish(payload_manifest());
    FakeHttpResponse slow = ok_body(kClientV1);
    slow.delay = 5s;
    f.transport.route("GET", client_url("1.0.0"), slow);
    f.start();
    auto handle = f.store->start_ensure("payload", DisconnectPolicy::Detached);
    REQUIRE(handle);
    f.strand.run_until([&] { return f.transport.in_flight() == 1; });
    REQUIRE(f.ops.cancel(handle->id(), CancelReason::User));
    CHECK(std::holds_alternative<Cancelled>(f.run_op(*handle)));
    f.strand.run_until([&] { return f.transport.in_flight() == 0; });
    f.strand.run_until([&] { return f.info("payload", "1.0.0")->state == ComponentState::Missing; });
}

TEST_CASE("runtimes are unpacked into their own directory", "[components][store]") {
    Fixture f;
    f.publish(payload_manifest());
    f.start();
    auto pinned = f.acquire_runtime("kron-10");
    REQUIRE(pinned);
    CHECK(pinned->runtime.kind == RuntimeKind::KronWine);
    CHECK(pinned->runtime.root == f.layout.components_dir() / "runtime" / sha_hex(wine_archive()));
    CHECK(read_text(pinned->runtime.root / "bin" / "wine") == "#!wine");
    CHECK(read_text(pinned->runtime.root / "lib" / "wine" / "ntdll.so") == "ntdll");
    CHECK(std::ranges::find(f.phases, std::string("extract")) != f.phases.end());
    CHECK(gone(f.layout.components_dir() / ".staging" / sha_hex(wine_archive())));
    const auto info = f.info("kron-10", "1");
    REQUIRE(info);
    CHECK(info->state == ComponentState::Ready);
    CHECK(info->size_bytes == std::string("#!wine").size() + std::string("ntdll").size());

    const auto wrong = f.acquire_runtime("missing");
    REQUIRE_FALSE(wrong);
    CHECK(wrong.error().id == "components.unknown_runtime");
}

TEST_CASE("an archive entry that escapes the runtime root fails the unpack", "[components][store]") {
    const std::string evil = make_tar_gz({{"bin/wine", "x"}, {"../../escape", "boom"}});
    ManifestJson json;
    json.runtime("evil", "kron_wine", "linux", "https://cdn.test/evil.tar.gz", evil);
    Fixture f;
    f.publish(json);
    f.transport.route("GET", "https://cdn.test/evil.tar.gz", ok_body(evil));
    f.start();
    const auto pinned = f.acquire_runtime("evil");
    REQUIRE_FALSE(pinned);
    CHECK(pinned.error().id == "components.extract_failed");
    CHECK(gone(f.layout.components_dir() / "escape"));
    CHECK(gone(f.layout.components_dir() / "runtime" / sha_hex(evil)));
    CHECK(f.info("evil", "1")->state == ComponentState::Missing);
}

TEST_CASE("garbage collection keeps the pinned, selected and last good versions", "[components][store]") {
    Fixture f;
    f.publish(payload_manifest());
    f.start();
    auto v1 = f.acquire_payload();
    REQUIRE(v1);
    f.store->mark_good(v1->pin);
    CHECK(f.info("payload", "1.0.0")->last_good);
    v1->pin.release();

    ManifestJson next;
    next.serial = 2;
    next.payload("2.0.0", VersionStreams::payload_abi, kClientV2, kWinhost);
    f.refresh_to(next);
    f.strand.run_until([&] { return f.info("payload", "2.0.0").has_value(); });
    // Not selected any more, but the last version that completed a good session.
    const auto kept = f.info("payload", "1.0.0");
    REQUIRE(kept);
    CHECK_FALSE(kept->selected);
    CHECK(kept->state == ComponentState::Ready);

    auto v2 = f.acquire_payload();
    REQUIRE(v2);
    CHECK(v2->set.ref.version == "2.0.0");
    // winhost did not change, so its stored copy is shared.
    CHECK(f.requests_to(winhost_url("2.0.0")) == 0);
    f.store->mark_good(v2->pin);
    f.strand.run_until([&] { return !f.info("payload", "1.0.0").has_value(); });
    // This check queues behind the removal on the single worker.
    REQUIRE(f.hold(v2->set, PayloadRole::ClientDll));
    CHECK(gone(f.payload_file(kClientV1, PayloadRole::ClientDll)));
    CHECK_FALSE(f.info("payload", "1.0.0"));
    CHECK(read_text(f.payload_file(kWinhost, PayloadRole::Winhost)) == kWinhost);
    const auto current = f.info("payload", "2.0.0");
    REQUIRE(current);
    CHECK(current->last_good);
    CHECK(current->pins == 1);

    SECTION("a shared file of the collected version stays watched") {
        std::filesystem::remove(f.payload_file(kWinhost, PayloadRole::Winhost));
        f.watcher.emit(ports::FileChange{f.payload_file(kWinhost, PayloadRole::Winhost), ports::FileChangeKind::Removed});
        f.wait_event([](const ComponentChangedEvent& e) {
            return e.problem && e.problem->recovery == RecoveryState::Recovered;
        });
        CHECK(read_text(f.payload_file(kWinhost, PayloadRole::Winhost)) == kWinhost);
    }
}

TEST_CASE("a restarted store reads its index, checks every file and clears leftovers", "[components][store]") {
    testing::ScratchDir dir = make_scratch();
    {
        Fixture first(dir.path());
        first.publish(payload_manifest());
        first.start();
        REQUIRE(first.acquire_payload());
        REQUIRE(first.acquire_runtime("kron-10"));
        const NativePath index = first.layout.components_dir() / "index.json";
        first.settle();
        const std::string text = read_text(index);
        CHECK(text.find("kron-10") != std::string::npos);
        CHECK(text.find("1.0.0") != std::string::npos);
    }

    Fixture second(dir.path());
    const NativePath components = second.layout.components_dir();
    std::filesystem::remove(second.payload_file(kClientV1, PayloadRole::ClientDll));
    write_text(components / "payload" / std::string(64, 'a') / "rb_client.dll", "orphan");
    write_text(components / ".staging" / "partial", "half");
    second.publish(payload_manifest());
    second.start();

    CHECK(gone(components / "payload" / std::string(64, 'a')));
    CHECK(gone(components / ".staging" / "partial"));
    CHECK(second.info("payload", "1.0.0")->state == ComponentState::Broken);
    CHECK(second.info("kron-10", "1")->state == ComponentState::Ready);

    // A broken version found at startup is re-fetched quietly on its next use.
    auto pinned = second.acquire_payload();
    REQUIRE(pinned);
    CHECK(read_text(second.payload_file(kClientV1, PayloadRole::ClientDll)) == kClientV1);
    CHECK(second.info("payload", "1.0.0")->state == ComponentState::Ready);
    second.drain();
    for (const ComponentChangedEvent& event : second.seen) CHECK_FALSE(event.problem);
}

TEST_CASE("an unreadable index starts the store empty", "[components][store]") {
    Fixture f;
    f.publish(payload_manifest());
    write_text(f.layout.components_dir() / "index.json", "{not json");
    f.start();
    CHECK(f.info("payload", "1.0.0")->state == ComponentState::Missing);
}

TEST_CASE("two versions that share a file download it into their own part files", "[components][store]") {
    Fixture f;
    f.publish(payload_manifest());
    f.start();
    auto v1 = f.acquire_payload();
    REQUIRE(v1);
    ManifestJson next = payload_manifest(2);
    next.payloads.clear();
    next.payload("2.0.0", VersionStreams::payload_abi, kClientV2, kWinhost);
    f.refresh_to(next);

    std::filesystem::remove(f.payload_file(kWinhost, PayloadRole::Winhost));
    for (const std::string& version : {std::string("1.0.0"), std::string("2.0.0")}) {
        FakeHttpResponse slow = ok_body(kWinhost);
        slow.delay = 1s;
        f.transport.route("GET", winhost_url(version), slow);
    }
    auto ensure = f.store->start_ensure("payload", DisconnectPolicy::Detached);
    REQUIRE(ensure);
    std::optional<Result<IntegrityHold>> held;
    f.store->hold(v1->set, PayloadRole::Winhost, {}, nullptr, [&](Result<IntegrityHold> r) { held = std::move(r); });
    // Both versions now download the same winhost at once.
    f.strand.run_until([&] {
        return f.requests_to(winhost_url("1.0.0")) == 2 && f.requests_to(winhost_url("2.0.0")) == 1;
    });
    f.strand.advance(1s);
    CHECK(std::holds_alternative<Completed<std::any>>(f.run_op(*ensure)));
    f.strand.run_until([&] { return held.has_value(); });
    REQUIRE(*held);

    CHECK(read_text(f.payload_file(kWinhost, PayloadRole::Winhost)) == kWinhost);
    CHECK(f.requests_to(winhost_url("1.0.0")) == 2);
    CHECK(f.requests_to(winhost_url("2.0.0")) == 1);
    f.drain();
    const NativePath staging = f.layout.components_dir() / ".staging";
    for (const ComponentChangedEvent& event : f.seen)
        if (event.problem) CHECK(event.problem->file.parent_path() != staging);
}

TEST_CASE("a fetch asked for before load starts once the index is read", "[components][store]") {
    Fixture f;
    f.publish(payload_manifest());
    f.create();
    std::optional<Result<PinnedPayload>> pinned;
    f.store->acquire_payload(SessionId{}, {}, nullptr, [&](Result<PinnedPayload> r) { pinned = std::move(r); });
    std::optional<Result<void>> loaded;
    f.store->load([&](Result<void> result) { loaded = std::move(result); });
    f.strand.run_until([&] { return pinned.has_value() && loaded.has_value(); });
    REQUIRE(*loaded);
    REQUIRE(*pinned);
    const auto info = f.info("payload", "1.0.0");
    REQUIRE(info);
    CHECK(info->state == ComponentState::Ready);
    CHECK(info->pins == 1);
}

TEST_CASE("an index of a newer schema leaves the store read-only", "[components][store]") {
    Fixture f;
    f.publish(payload_manifest());
    const NativePath components = f.layout.components_dir();
    const std::string newer = R"({"schema":2,"payloads":[],"runtimes":[]})";
    write_text(components / "index.json", newer);
    const NativePath foreign = components / "payload" / std::string(64, 'b') / "rb_client.dll";
    write_text(foreign, "named by the newer index");
    f.start();
    CHECK(read_text(foreign) == "named by the newer index");
    CHECK(f.info("payload", "1.0.0")->state == ComponentState::Missing);

    auto pinned = f.acquire_payload();
    REQUIRE(pinned);
    pinned->pin.release();
    // The single worker ran any index write before this acquire's checks.
    REQUIRE(f.acquire_payload());
    CHECK(read_text(components / "index.json") == newer);
    CHECK(read_text(foreign) == "named by the newer index");
    const auto removed = f.store->start_remove("payload", DisconnectPolicy::Detached);
    REQUIRE_FALSE(removed);
    CHECK(removed.error().id == "components.store_failed");
}
