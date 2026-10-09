#include "reboot/engine/engine_host.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <utility>
#include <variant>

#include <boost/asio/executor_work_guard.hpp>
#include <boost/asio/io_context.hpp>

#include "current_process.hpp"
#include "engine_adapters.hpp"
#include "host_platform.hpp"
#include "messages.hpp"
#include "reboot/components/manifest_platform.hpp"
#include "reboot/engine/engine_info.hpp"
#include "reboot/engine/engine_lock.hpp"
#include "reboot/engine/engine_services.hpp"
#include "reboot/foundation/clock.hpp"
#include "reboot/foundation/executor.hpp"
#include "reboot/foundation/log.hpp"
#include "reboot/foundation/paths.hpp"
#include "reboot/foundation/random.hpp"
#include "reboot/ipc/endpoint.hpp"
#include "reboot/ipc/ipc_codec.hpp"
#include "reboot/logging/file_log_sink.hpp"
#include "reboot/logging/log_files_in_use.hpp"
#include "reboot/logging/log_ring.hpp"
#include "reboot/logging/terminate_handler.hpp"
#include "reboot/logging/wine_log_files.hpp"
#include "reboot/net/curl_http_transport.hpp"
#include "reboot/net/msquic_transport.hpp"
#include "reboot/process/env_layer.hpp"
#include "reboot/storage/data_root.hpp"
#include "reboot/storage/document_store.hpp"
#include "reboot/storage/resume_document.hpp"

namespace rb::engine {

namespace {

constexpr std::size_t kLoggerBudget = std::size_t{8} << 20;
constexpr std::chrono::milliseconds kSignalPoll{250};
constexpr std::chrono::seconds kSelfTestDeadline{10};

#ifdef _WIN32
constexpr std::string_view kEngineExe = "reboot-engine.exe";
#else
constexpr std::string_view kEngineExe = "reboot-engine";
#endif

std::atomic<bool> g_signalled{false};

void on_signal(int) { g_signalled.store(true); }

// Before the logger runs only stderr is left, and only the message id is printed.
[[nodiscard]] int early_failure(const Diagnostic& diag) {
    std::fprintf(stderr, "reboot-engine: %s\n", diag.id.c_str());
    return exit_code_for(diag);
}

[[nodiscard]] int failure(const Diagnostic& diag) {
    REBOOT_LOG_ERROR(Engine, "the engine cannot start: {}", diag.id);
    Logger::flush();
    return exit_code_for(diag);
}

// POSIX hands every child a filtered copy of the engine's own environment; Windows children get
// the user's environment block from the platform instead.
[[nodiscard]] ports::EnvBlock user_environment() {
    ports::EnvBlock block;
    if constexpr (!kWindowsHost) {
        for (const process::EnvNamePattern& pattern : process::kPosixBaseNames) {
            const std::string name(pattern.text);
            if (std::optional<std::string> value = own_environment(name.c_str())) block.vars.emplace_back(name, std::move(*value));
        }
    }
    return block;
}

[[nodiscard]] std::optional<u32> numeric_uid(const std::string& user_id) {
    if (user_id.empty() || user_id.size() > 10) return std::nullopt;
    u64 value = 0;
    for (const char c : user_id) {
        if (c < '0' || c > '9') return std::nullopt;
        value = value * 10 + static_cast<u64>(c - '0');
    }
    if (value > 0xFFFFFFFFull) return std::nullopt;
    return static_cast<u32>(value);
}

// The origin restart_when_idle or an update recorded, when resume.json exists.
[[nodiscard]] std::optional<EngineOrigin> recorded_origin(ports::IFileSystem& fs, WorkerPool& workers, Executor& strand,
                                                          const IClock& clock, const AppLayout& layout) {
    storage::DocumentStore<storage::ResumeDocument> resume(fs, workers, strand, clock, layout.resume_file());
    const storage::LoadReport report = resume.load();
    if (report.source != storage::LoadSource::Primary && report.source != storage::LoadSource::Backup) return std::nullopt;
    return resume.get().origin;
}

// A Hello to our own endpoint answered with a HelloAck: the strand, the listener and the codec all work.
[[nodiscard]] Result<void> self_test(ports::IIpcConnector* connector, const EngineInfo& info) {
    const auto failed = [&info](std::optional<Diagnostic> cause) {
        DiagBuilder builder = make_diag(ErrorDomain::Engine, msg::kSelfTestFailed).arg("endpoint", info.endpoint);
        if (cause) std::move(builder).cause(std::move(*cause));
        return std::unexpected(std::move(builder).build());
    };
    if (connector == nullptr) return failed(std::nullopt);
    Result<std::unique_ptr<ports::IByteStream>> stream = connector->connect(info.endpoint, kSelfTestDeadline);
    if (!stream) return failed(std::move(stream.error()));

    struct Exchange {
        std::mutex mutex;
        std::condition_variable changed;
        ipc::IpcCodec codec;
        bool acknowledged = false;
        bool ended = false;
    };
    auto exchange = std::make_shared<Exchange>();
    (*stream)->on_read([exchange](std::span<const u8> bytes) {
        const std::scoped_lock lock(exchange->mutex);
        Result<std::vector<ipc::EngineMessage>> messages = exchange->codec.feed_from_engine(bytes);
        if (!messages) exchange->ended = true;
        else
            for (const ipc::EngineMessage& message : *messages)
                if (std::holds_alternative<contracts::ipc::HelloAck>(message)) exchange->acknowledged = true;
        exchange->changed.notify_all();
    });
    (*stream)->on_close([exchange] {
        const std::scoped_lock lock(exchange->mutex);
        exchange->ended = true;
        exchange->changed.notify_all();
    });
    contracts::ipc::Hello hello;
    hello.client_kind = contracts::ipc::ClientKind::Test;
    hello.client_build = info.build;
    hello.abi_version = (u32{VersionStreams::abi_major} << 16) | VersionStreams::abi_minor;
    hello.pid = info.self.pid;
    (*stream)->write(ipc::IpcCodec::encode(hello));
    bool acknowledged = false;
    {
        std::unique_lock lock(exchange->mutex);
        exchange->changed.wait_for(lock, kSelfTestDeadline, [&] { return exchange->acknowledged || exchange->ended; });
        acknowledged = exchange->acknowledged;
    }
    if (acknowledged) (*stream)->write(ipc::IpcCodec::encode(contracts::ipc::Goodbye{contracts::ipc::GoodbyeReason::Normal}));
    (*stream)->close();
    if (!acknowledged) return failed(std::nullopt);
    return {};
}

// SIGINT and SIGTERM only set a flag; the strand looks at it, since a handler may not post.
class SignalWatch {
public:
    SignalWatch(TimerService& timers, EngineServices& services) : timers_(timers), services_(services) {}

    void arm() {
        timer_ = timers_.after(kSignalPoll, [this] {
            if (!delivered_ && g_signalled.load()) {
                delivered_ = true;
                services_.on_os_signal();
            }
            arm();
        });
    }

private:
    TimerService& timers_;
    EngineServices& services_;
    TimerHandle timer_;
    bool delivered_ = false;
};

}  // namespace

struct EngineHost::Impl {
    EngineCommandLine command_line;
    ports::PlatformServices platform;
    ports::PeerIdentity self;
    std::unique_ptr<ports::IIpcConnector> self_test;
};

EngineHost::EngineHost(EngineCommandLine command_line, ports::PlatformServices platform, ports::PeerIdentity self,
                       std::unique_ptr<ports::IIpcConnector> self_test)
    : impl_(std::make_unique<Impl>(Impl{command_line, std::move(platform), std::move(self), std::move(self_test)})) {}

EngineHost::~EngineHost() = default;

int EngineHost::run() {
    Impl& impl = *impl_;
    ports::PlatformServices& platform = impl.platform;

    // Step 1.
    Result<DataRoot> root = resolve_data_root(*platform.paths, own_environment("REBOOT_LAUNCHER_HOME"));
    if (!root) return early_failure(root.error());
    const AppLayout layout(*root, *platform.paths);
    const InstallLayout install = locate_install(*platform.paths, std::nullopt);
    Result<storage::DataRootReport> prepared = storage::prepare_data_root(layout, install, *platform.paths, *platform.fs);
    if (!prepared) return early_failure(prepared.error());

    // Step 2: a second engine for this data root leaves the clients to the first.
    Result<std::optional<EngineLock>> lock = EngineLock::try_acquire(*platform.fs, layout);
    if (!lock) return early_failure(lock.error());
    if (!*lock) return static_cast<int>(EngineExit::Ok);

    // Step 3.
    SystemClock clock;
    static logging::LogFilesInUse files_in_use;
    Logger::install(kLoggerBudget);
    logging::install_terminate_handler();
    auto ring = std::make_unique<logging::LogRing>();
    logging::LogRing& log_ring = *ring;
    Logger::add_sink(std::move(ring));
    const u32 pid = current_process_id();
    const logging::LogFileGroup group{std::chrono::floor<std::chrono::seconds>(clock.system_now()), pid};
    std::unique_ptr<logging::WineLogFiles> wine_logs;
    if constexpr (!kWindowsHost)
        wine_logs = std::make_unique<logging::WineLogFiles>(logging::WineLogOptions{layout.logs_dir(), group},
                                                            *platform.logs, files_in_use);
    logging::FileLogSink* session_log = nullptr;
    if (Result<std::unique_ptr<logging::FileLogSink>> sink =
            logging::FileLogSink::open(logging::FileLogOptions{.dir = layout.logs_dir(), .group = group, .role = "engine"}, clock,
                                       *platform.logs, files_in_use, std::move(wine_logs))) {
        session_log = sink->get();
        Logger::add_sink(std::move(*sink));
    } else {
        std::fprintf(stderr, "reboot-engine: logging to files is off: %s\n", sink.error().id.c_str());
    }
    const logging::RegisteredThread strand_thread("strand");

    Strand strand;
    TimerService timers(clock, strand);
    WorkerPool workers(std::max(2u, std::thread::hardware_concurrency() / 2));
    boost::asio::io_context io;
    auto work = boost::asio::make_work_guard(io);
    std::thread io_thread([&io] {
        const logging::RegisteredThread named("io");
        io.run();
    });
    const auto stop_threads = [&] {
        work.reset();
        io.stop();
        if (io_thread.joinable()) io_thread.join();
        workers.shutdown();
    };

    Result<std::unique_ptr<net::CurlHttpTransport>> http = net::CurlHttpTransport::create(*platform.system);
    if (!http) {
        stop_threads();
        const int code = failure(http.error());
        Logger::shutdown();
        return code;
    }
    std::unique_ptr<ports::IQuicTransport> quic;
    if (Result<std::unique_ptr<net::MsQuicTransport>> msquic = net::MsQuicTransport::create(*platform.system)) {
        quic = std::move(*msquic);
    } else {
        REBOOT_LOG_WARN(Net, "the server browser is unavailable: {}", msquic.error().id);
        quic = std::make_unique<UnavailableQuicTransport>(std::move(msquic.error()));
    }

    EngineInfo info;
    info.app_version = SemVer::parse(info.build).value_or(SemVer{});
    // Random per start, so a reconnecting client tells a restarted engine from the one it knew.
    while (info.epoch.value == 0)
        for (const u8 byte : random_bytes<8>(*platform.random)) info.epoch.value = (info.epoch.value << 8) | byte;
    info.self.pid = pid;
    info.self.created = clock.system_now();
    info.self.image_path = platform.paths->exe_dir() / std::string(kEngineExe);
    info.canonical_root = canonical_root(*root);
    info.root_hash16 = root_hash16(info.canonical_root);
    Result<std::string> endpoint = ipc::endpoint_for(impl.self, info.root_hash16);
    if (!endpoint) {
        stop_threads();
        const int code = failure(endpoint.error());
        Logger::shutdown();
        return code;
    }
    info.endpoint = std::move(*endpoint);
    info.origin = resumed_origin(impl.command_line.origin, recorded_origin(*platform.fs, workers, strand, clock, layout));
    info.os = platform.system->os();
    REBOOT_LOG_INFO(Engine, "engine {} starting {} for {}", info.build, origin_name(info.origin), info.root_hash16);

#if !defined(_WIN32)
    if (!platform.runner)
        platform.runner = ports::make_runner_platform(layout, *platform.processes, user_environment());
#endif

    int exit_code = static_cast<int>(EngineExit::Ok);
    std::optional<Diagnostic> memory_only = prepared->reason;
    if (prepared->mode != storage::StorageMode::InMemory) memory_only.reset();
    std::optional<SignalWatch> signals;
    {
        EngineServices services(EngineRuntime{
            .platform = platform,
            .http = **http,
            .quic = *quic,
            .clock = clock,
            .strand = strand,
            .timers = timers,
            .workers = workers,
            .io = io,
            .log_ring = log_ring,
            .session_log = session_log,
            .layout = layout,
            .install = install,
            .info = info,
            .user_environment = user_environment(),
            .uid = numeric_uid(impl.self.user_id),
            .on_exit =
                [&exit_code, &strand](EngineExit exit) {
                    exit_code = static_cast<int>(exit);
                    strand.stop();
                },
            .overrides = {}});

        // Steps 4 to 8.
        static_cast<void>(services.open_stores(std::move(memory_only)));
        static_cast<void>(services.reap_orphans());
        services.load_components_and_catalog();
        services.reconcile_integration();
        if (Result<void> opened = services.open_endpoint(); !opened) {
            stop_threads();
            const int code = failure(opened.error());
            Logger::shutdown();
            return code;
        }
        Result<updates::StartupResume> resume = services.begin_resume();
        if (!resume) REBOOT_LOG_WARN(Update, "the update marker and resume.json were not read: {}", resume.error().id);
        const updates::StartupResume startup = resume ? std::move(*resume) : updates::StartupResume{};

        std::signal(SIGINT, on_signal);
        std::signal(SIGTERM, on_signal);
        signals.emplace(timers, services);

        // Steps 9 to 11, on the strand.
        strand.post([&] {
            services.start();
            signals->arm();
            const auto finish = [&services, &startup](Result<void> tested) {
                services.apply_resume(startup, std::move(tested));
                if (Result<void> written = services.write_runtime(); !written)
                    REBOOT_LOG_WARN(Engine, "runtime.json was not written: {}", written.error().id);
            };
            if (startup.verdict != updates::MarkerVerdict::SelfTest) {
                finish(Result<void>{});
                return;
            }
            workers.submit<std::monostate>(
                [connector = impl.self_test.get(), &info](CancelToken) -> Result<std::monostate> {
                    if (Result<void> tested = self_test(connector, info); !tested) return std::unexpected(std::move(tested.error()));
                    return std::monostate{};
                },
                CancelToken{}, strand,
                [finish](Result<std::monostate> tested) mutable {
                    finish(tested ? Result<void>{} : Result<void>(std::unexpected(std::move(tested.error()))));
                });
        });
        strand.run();
        signals.reset();
        stop_threads();
    }
    REBOOT_LOG_INFO(Engine, "engine stopped with code {}", exit_code);
    Logger::shutdown();
    return exit_code;
}

}  // namespace rb::engine
