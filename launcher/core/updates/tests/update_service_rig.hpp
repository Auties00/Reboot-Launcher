#pragma once

#include <openssl/evp.h>

#include <any>
#include <array>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <deque>
#include <filesystem>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <boost/json/array.hpp>
#include <boost/json/object.hpp>
#include <boost/json/serialize.hpp>
#include <catch2/catch_test_macros.hpp>

#include "reboot/components/manifest_platform.hpp"
#include "reboot/components/manifest_service.hpp"
#include "reboot/contracts/ipc.hpp"
#include "reboot/foundation/clock.hpp"
#include "reboot/foundation/events.hpp"
#include "reboot/foundation/executor.hpp"
#include "reboot/foundation/operation.hpp"
#include "reboot/foundation/paths.hpp"
#include "reboot/foundation/sha256.hpp"
#include "reboot/foundation/user_request.hpp"
#include "reboot/net/host_tls_memory.hpp"
#include "reboot/net/http_client.hpp"
#include "reboot/net/resumable_downloader.hpp"
#include "reboot/storage/document_store.hpp"
#include "reboot/storage/resume_document.hpp"
#include "reboot/storage/state_document.hpp"
#include "reboot/testing/event_recorder.hpp"
#include "reboot/testing/fake_disk_info.hpp"
#include "reboot/testing/fake_http_transport.hpp"
#include "reboot/testing/fake_platform_paths.hpp"
#include "reboot/testing/fake_random.hpp"
#include "reboot/testing/fake_update_applier.hpp"
#include "reboot/testing/in_memory_file_system.hpp"
#include "reboot/testing/scratch_dir.hpp"
#include "reboot/trust/key_ring.hpp"
#include "reboot/trust/serial_guard.hpp"
#include "reboot/trust/signed_document_kind.hpp"
#include "reboot/updates/activity_probe.hpp"
#include "reboot/updates/update_event.hpp"
#include "reboot/updates/update_service.hpp"

namespace reboot::updates::test {

// The strand beside real worker threads: they post from their threads, timed tasks follow the
// ManualClock, and only the test thread runs anything.
class TestStrand final : public Executor {
public:
    explicit TestStrand(ManualClock& clock) : clock_(clock) {}

    void post(UniqueFunction<void()> task) override {
        const std::scoped_lock lock(mutex_);
        ready_.push_back(std::move(task));
        posted_.notify_one();
    }
    void post_at(SteadyTime when, UniqueFunction<void()> task) override {
        const std::scoped_lock lock(mutex_);
        timed_.emplace(when, std::move(task));
    }

    std::size_t run_ready() {
        std::size_t ran = 0;
        while (UniqueFunction<void()> task = next(false)) {
            task();
            ++ran;
        }
        return ran;
    }

    // Runs tasks, waiting for other threads to post, until `done` holds.
    template <class Done>
    void run_until(Done&& done) {
        run_ready();
        while (!done()) {
            UniqueFunction<void()> task = next(true);
            REQUIRE(static_cast<bool>(task));
            task();
            run_ready();
        }
    }

    // Moves time one due task at a time, so each timer runs at its own deadline.
    void advance(std::chrono::steady_clock::duration by) {
        const SteadyTime end = clock_.steady_now() + by;
        run_ready();
        while (true) {
            SteadyTime due{};
            {
                const std::scoped_lock lock(mutex_);
                if (timed_.empty() || timed_.begin()->first > end) break;
                due = timed_.begin()->first;
            }
            if (due > clock_.steady_now()) clock_.advance(due - clock_.steady_now());
            run_ready();
        }
        if (end > clock_.steady_now()) clock_.advance(end - clock_.steady_now());
        run_ready();
    }

private:
    UniqueFunction<void()> next(bool wait) {
        std::unique_lock lock(mutex_);
        const SteadyTime now = clock_.steady_now();
        while (!timed_.empty() && timed_.begin()->first <= now) {
            ready_.push_back(std::move(timed_.begin()->second));
            timed_.erase(timed_.begin());
        }
        // A bound, not a sleep: a missing post fails the test instead of hanging it.
        if (wait && !posted_.wait_for(lock, std::chrono::seconds{20}, [this] { return !ready_.empty(); })) return {};
        if (ready_.empty()) return {};
        UniqueFunction<void()> task = std::move(ready_.front());
        ready_.pop_front();
        return task;
    }

    ManualClock& clock_;
    std::mutex mutex_;
    std::condition_variable posted_;
    std::deque<UniqueFunction<void()>> ready_;
    std::multimap<SteadyTime, UniqueFunction<void()>> timed_;
};

struct PkeyDeleter {
    void operator()(EVP_PKEY* key) const noexcept { EVP_PKEY_free(key); }
};
struct MdCtxDeleter {
    void operator()(EVP_MD_CTX* context) const noexcept { EVP_MD_CTX_free(context); }
};

// Signs release manifests the way the CI signer does: context prefix, then the body.
class TestSigner {
public:
    TestSigner() : key_(EVP_PKEY_Q_keygen(nullptr, nullptr, "ED25519")) {
        REQUIRE(key_);
        std::size_t size = public_key_.size();
        REQUIRE(EVP_PKEY_get_raw_public_key(key_.get(), public_key_.data(), &size) == 1);
    }

    [[nodiscard]] const trust::Ed25519PublicKey& public_key() const { return public_key_; }

    [[nodiscard]] std::string signature_file(std::string_view body) const {
        const std::string_view prefix = trust::signature_context(trust::SignedDocumentKind::ReleaseManifest);
        std::vector<u8> message(prefix.begin(), prefix.end());
        message.insert(message.end(), body.begin(), body.end());
        const std::unique_ptr<EVP_MD_CTX, MdCtxDeleter> context(EVP_MD_CTX_new());
        REQUIRE(EVP_DigestSignInit(context.get(), nullptr, nullptr, nullptr, key_.get()) == 1);
        std::array<u8, 64> signature{};
        std::size_t size = signature.size();
        REQUIRE(EVP_DigestSign(context.get(), signature.data(), &size, message.data(), message.size()) == 1);
        return "ed25519 " + trust::key_id_of(public_key_) + " " + to_hex(signature) + "\n";
    }

private:
    std::unique_ptr<EVP_PKEY, PkeyDeleter> key_;
    trust::Ed25519PublicKey public_key_{};
};

// Live sessions set by the test, plus every live Detached op as the engine's probe reports them.
class FakeActivity final : public IActivityProbe {
public:
    explicit FakeActivity(OpRegistry& ops) : ops_(ops) {}

    [[nodiscard]] ActivitySnapshot snapshot() const override {
        ActivitySnapshot out = current_;
        for (const LiveOp& op : ops_.live())
            if (op.policy == DisconnectPolicy::Detached)
                out.live.push_back(LiveActivity{.kind = LiveKind::DetachedOp, .op = op.op});
        return out;
    }
    void set_on_change(UniqueFunction<void()> on_change) override { on_change_ = std::move(on_change); }

    void set(ActivitySnapshot snapshot) {
        current_ = std::move(snapshot);
        if (on_change_) on_change_();
    }
    [[nodiscard]] bool listening() const { return static_cast<bool>(on_change_); }

private:
    OpRegistry& ops_;
    ActivitySnapshot current_;
    UniqueFunction<void()> on_change_;
};

inline constexpr std::string_view kManifestUrl = "https://updates.test/manifest.json";
inline constexpr std::string_view kSignatureUrl = "https://updates.test/manifest.json.sig";
inline constexpr std::string_view kPackageUrl = "https://cdn.test/RebootLauncher-1.1.0-full.nupkg";
inline constexpr std::string_view kMirrorUrl = "https://mirror.test/RebootLauncher-1.1.0-full.nupkg";
inline constexpr std::string_view kPackage = "velopack package bytes for 1.1.0";

[[nodiscard]] inline SemVer version(u32 major, u32 minor, u32 patch) { return SemVer{major, minor, patch, {}}; }

[[nodiscard]] inline std::vector<u8> bytes_of(std::string_view text) { return {text.begin(), text.end()}; }

struct AppSpec {
    std::string channel = "stable";
    SemVer version = test::version(1, 1, 0);
    std::optional<SemVer> min_supported;
    std::vector<std::string> urls{std::string(kPackageUrl)};
    std::string package = std::string(kPackage);
};

[[nodiscard]] inline std::string manifest_body(u64 serial, const std::vector<AppSpec>& apps) {
    const components::ManifestPlatform platform = components::build_platform();
    const char* os = platform.os == components::ManifestOs::Windows ? "windows"
                     : platform.os == components::ManifestOs::MacOs ? "macos"
                                                                    : "linux";
    const char* arch = platform.arch == components::ManifestArch::X64 ? "x64" : "arm64";
    boost::json::array apps_json;
    for (const AppSpec& app : apps) {
        boost::json::array urls;
        for (const std::string& url : app.urls) urls.emplace_back(url);
        boost::json::object package;
        package["urls"] = std::move(urls);
        package["sha256"] = to_hex(sha256(bytes_of(app.package)));
        package["size"] = app.package.size();
        boost::json::object entry;
        entry["platform"] = boost::json::object{{"os", os}, {"arch", arch}};
        entry["channel"] = app.channel;
        entry["version"] = app.version.to_string();
        if (app.min_supported) entry["min_supported"] = app.min_supported->to_string();
        entry["kind"] = "velopack";
        entry["package"] = std::move(package);
        apps_json.emplace_back(std::move(entry));
    }
    boost::json::object root;
    root["schema"] = VersionStreams::manifest_schema;
    root["serial"] = serial;
    root["expires_unix_ms"] = 4102444800000ull;
    root["apps"] = std::move(apps_json);
    return boost::json::serialize(root);
}

[[nodiscard]] inline testing::ScratchDir make_scratch() {
    testing::FakeRandom random(41);
    Result<testing::ScratchDir> dir = testing::ScratchDir::create(random, "reboot-updates");
    REQUIRE(dir);
    return std::move(*dir);
}

struct RigOptions {
    bool in_place = true;
    bool auto_check = false;
    SemVer installed = test::version(1, 0, 0);
    bool shim_counts_attempts = false;
    storage::UpdateChannel channel = storage::UpdateChannel::Stable;
    contracts::ipc::EngineOrigin origin = contracts::ipc::EngineOrigin::ServiceManager;
};

// One UpdateService over the real ManifestService, downloader and stores, on fakes.
struct Rig {
    explicit Rig(RigOptions rig_options = {}) : options(rig_options), applier(rig_options.in_place) {
        // The downloader writes a plain file, so its directory is real.
        std::filesystem::create_directories(layout.manifest_cache().parent_path() / "updates" / "1.1.0");
        std::filesystem::create_directories(layout.manifest_cache().parent_path() / "updates" / "1.2.0");
        // The engine creates state/ before any store writes there.
        fs.make_dir(layout.resume_file().parent_path());
        disk.add_volume(ports::VolumeInfo{.mount = scratch.path(), .free_bytes = 1u << 30, .total_bytes = 1u << 31});
        (void)resume.load();
        (void)state.load();
    }

    ~Rig() {
        service.reset();
        workers.shutdown();
    }

    void make_service() {
        // The old service drops its activity listener as it goes, so it goes first.
        service.reset();
        service = std::make_unique<UpdateService>(
            UpdateServiceDeps{
                .applier = applier,
                .fs = fs,
                .manifest = manifest,
                .downloader = downloader,
                .resume = resume,
                .state = state,
                .activity = activity,
                .workers = workers,
                .strand = strand,
                .clock = clock,
                .timers = timers,
                .ops = ops,
                .events = events,
                .requests = requests,
                .drain_for_update = [this] { ++drains; },
                .quiesce = [this](UniqueFunction<Result<void>()> apply) { pending_apply = std::move(apply); },
            },
            UpdateOptions{.installed = options.installed,
                          .origin = options.origin,
                          .channel = options.channel,
                          .auto_check = options.auto_check,
                          .shim_counts_attempts = options.shim_counts_attempts},
            layout);
    }

    void serve_manifest(u64 serial, const std::vector<AppSpec>& apps) {
        const std::string body = manifest_body(serial, apps);
        transport.route("GET", std::string(kManifestUrl), testing::FakeHttpResponse{.status = 200, .body = bytes_of(body)});
        transport.route("GET", std::string(kSignatureUrl),
                        testing::FakeHttpResponse{.status = 200, .body = bytes_of(signer.signature_file(body))});
    }

    // A `delay` holds the download until the test advances the clock, so the apply op is still running.
    void serve_package(std::string_view url = kPackageUrl, std::string_view body = kPackage,
                       std::chrono::milliseconds delay = {}) {
        transport.route("GET", std::string(url),
                        testing::FakeHttpResponse{.status = 200, .body = bytes_of(body), .delay = delay});
    }

    void hold_package() { serve_package(kPackageUrl, kPackage, std::chrono::seconds{1}); }

    // Lets a held download answer once its request is out.
    void release_package() {
        strand.run_until([&] { return transport.in_flight() > 0; });
        strand.advance(std::chrono::seconds{1});
    }

    [[nodiscard]] NativePath marker() const { return layout.update_marker(); }

    void write_marker(const PendingUpdateMarker& value) { fs.write(marker(), encode_marker(value)); }

    [[nodiscard]] std::optional<ErasedOutcome> outcome(const OpHandle& handle) const { return ops.outcome(handle.id()); }

    // Runs the strand until `handle` has an outcome.
    ErasedOutcome finish(const OpHandle& handle) {
        strand.run_until([&] { return ops.outcome(handle.id()).has_value(); });
        return *ops.outcome(handle.id());
    }

    template <class E>
    [[nodiscard]] std::vector<E> published(EventKind kind) {
        recorder.pump();
        std::vector<E> out;
        for (const E* payload : recorder.payloads<E>(kind)) out.push_back(*payload);
        return out;
    }

    RigOptions options;
    testing::ScratchDir scratch = make_scratch();
    ManualClock clock;
    TestStrand strand{clock};
    TimerService timers{clock, strand};
    EventBus events{EngineEpoch{1}};
    OpRegistry ops{clock, timers, events};
    UserRequestRegistry requests{events};
    testing::EventRecorder recorder{events};
    testing::FakeHttpTransport transport{strand, clock};
    testing::FakeRandom random{3};
    net::HostTlsMemory tls{{}, nullptr};
    net::HttpClient http{transport, tls, strand, timers, random};
    testing::InMemoryFileSystem fs;
    testing::FakeDiskInfo disk;
    testing::FakeUpdateApplier applier;
    FakeActivity activity{ops};
    TestSigner signer;
    trust::KeyRing keys{trust::SignedDocumentKind::ReleaseManifest, trust::PinnedKeys{signer.public_key(), std::nullopt}};
    trust::SerialGuard serials{trust::SignedDocumentKind::ReleaseManifest, 0, [](u64) -> Result<void> { return {}; }};
    testing::FakePlatformPaths paths{scratch.path()};
    AppLayout layout{DataRoot{scratch.path() / "data", true}, paths};
    InstallLayout install = locate_install(paths, std::nullopt);
    WorkerPool workers{2};
    net::ResumableDownloader downloader{http, disk, fs, workers, strand, timers, clock};
    components::ManifestService manifest{
        components::ManifestServiceDeps{fs, http, workers, strand, clock, keys, serials},
        components::ManifestOptions{.url = std::string(kManifestUrl), .signature_url = std::string(kSignatureUrl), .channel = "stable"},
        layout, install};
    storage::DocumentStore<storage::ResumeDocument> resume{fs, workers, strand, clock, layout.resume_file()};
    storage::DocumentStore<storage::StateDocument> state{fs, workers, strand, clock, layout.state_file()};
    int drains = 0;
    UniqueFunction<Result<void>()> pending_apply;
    std::unique_ptr<UpdateService> service;
};

template <class T>
[[nodiscard]] const T* completed(const ErasedOutcome& outcome) {
    const auto* done = std::get_if<Completed<std::any>>(&outcome);
    return done == nullptr ? nullptr : std::any_cast<T>(&done->value);
}

[[nodiscard]] inline const Diagnostic* failed(const ErasedOutcome& outcome) {
    const auto* failure = std::get_if<Failed>(&outcome);
    return failure == nullptr ? nullptr : &failure->error;
}

}  // namespace reboot::updates::test
