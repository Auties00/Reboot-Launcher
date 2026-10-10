#pragma once

#include <algorithm>
#include <chrono>
#include <expected>
#include <memory>
#include <optional>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include <boost/asio/executor_work_guard.hpp>
#include <boost/asio/io_context.hpp>
#include <catch2/catch_test_macros.hpp>

#include "engine_test_support.hpp"
#include "reboot/api/codec.hpp"
#include "reboot/api/v1/method_table.hpp"
#include "reboot/api/v1/host.hpp"
#include "reboot/api/v1/requests.hpp"
#include "reboot/engine/api_router.hpp"
#include "reboot/engine/engine_info.hpp"
#include "reboot/engine/engine_lifecycle.hpp"
#include "reboot/engine/engine_services.hpp"
#include "reboot/foundation/clock.hpp"
#include "reboot/foundation/executor.hpp"
#include "reboot/foundation/paths.hpp"
#include "reboot/foundation/random.hpp"
#include "reboot/foundation/version.hpp"
#include "reboot/logging/log_ring.hpp"
#include "reboot/net/port_mapping_gateway.hpp"
#include "reboot/ports/platform_services.hpp"
#include "reboot/storage/data_root.hpp"
#include "reboot/testing/api_test_client.hpp"
#include "reboot/testing/fake_backend.hpp"
#include "reboot/testing/fake_disk_info.hpp"
#include "reboot/testing/fake_file_watcher.hpp"
#include "reboot/testing/fake_game_server.hpp"
#include "reboot/testing/fake_http_transport.hpp"
#include "reboot/testing/fake_loopback_peer_inspector.hpp"
#include "reboot/testing/fake_os.hpp"
#include "reboot/testing/fake_platform_paths.hpp"
#include "reboot/testing/fake_port_inspector.hpp"
#include "reboot/testing/fake_prereqs.hpp"
#include "reboot/testing/fake_quic_transport.hpp"
#include "reboot/testing/fake_random.hpp"
#include "reboot/testing/fake_registrar.hpp"
#include "reboot/testing/fake_resolver.hpp"
#include "reboot/testing/fake_secret_store.hpp"
#include "reboot/testing/fake_security_probe.hpp"
#include "reboot/testing/fake_session_host.hpp"
#include "reboot/testing/fake_shell.hpp"
#include "reboot/testing/fake_system_info.hpp"
#include "reboot/testing/fake_update_applier.hpp"
#include "reboot/testing/in_memory_ipc.hpp"
#include "reboot/testing/in_memory_log_file_system.hpp"
#include "reboot/testing/scratch_dir.hpp"
#include "reboot/testing/scripted_process_launcher.hpp"

namespace rb::engine::test {

// No gateway answers, as on a network without UPnP or NAT-PMP.
class AbsentGateway final : public net::IPortMappingGateway {
public:
    explicit AbsentGateway(net::MappingMethod method) : method_(method) {}
    [[nodiscard]] net::MappingMethod method() const noexcept override { return method_; }
    std::expected<net::GatewayInfo, net::GatewayError> discover(std::chrono::milliseconds, const CancelToken&) override {
        return std::unexpected(net::GatewayError{net::GatewayErrorCode::NoGateway});
    }
    std::expected<net::PortMapping, net::GatewayError> add(const net::GatewayMappingRequest&, std::chrono::milliseconds) override {
        return std::unexpected(net::GatewayError{net::GatewayErrorCode::NoGateway});
    }
    std::expected<void, net::GatewayError> remove(const net::PortMapping&, std::chrono::milliseconds) override {
        return std::unexpected(net::GatewayError{net::GatewayErrorCode::NoGateway});
    }
    std::expected<std::vector<net::GatewayEntry>, net::GatewayError> list(std::chrono::milliseconds) override {
        return std::unexpected(net::GatewayError{net::GatewayErrorCode::NoGateway});
    }

private:
    net::MappingMethod method_;
};

// A composed engine on fakes: real worker and I/O threads and a real scratch directory, every
// other port faked, and the engine strand run by the test thread on manual time. Windows-shaped:
// the game runs natively under FakeSessionHost.
class EngineRig {
public:
    EngineRig() : strand(clock), timers(clock, strand), ipc(strand, ports::PeerIdentity{"1000", 4242}), http(strand, clock), quic(strand) {
        // Logs, leases and bans compare against wall time, which the epoch would make odd.
        clock.set_system(std::chrono::system_clock::time_point{std::chrono::seconds{1'790'000'000}});
        OsRandom random;
        Result<testing::ScratchDir> dir = testing::ScratchDir::create(random, "reboot-engine-test");
        REQUIRE(dir);
        scratch.emplace(std::move(*dir));
        strand.hold_time_for(workers);
        work.emplace(boost::asio::make_work_guard(io));
        io_thread = std::thread([this] { io.run(); });

        auto paths_owner = std::make_unique<testing::FakePlatformPaths>(scratch->path());
        paths = paths_owner.get();
        platform.paths = std::move(paths_owner);
        auto fs_owner = std::make_unique<DiskFileSystem>();
        fs = fs_owner.get();
        platform.fs = std::move(fs_owner);
        platform.logs = std::make_unique<testing::InMemoryLogFileSystem>(clock);
        platform.watcher = std::make_unique<testing::FakeFileWatcher>(strand);
        auto disk = std::make_unique<testing::FakeDiskInfo>();
        disk->add_volume({scratch->path().root_path(), "fake", "fakefs", u64{1} << 39, u64{1} << 40});
        platform.disk = std::move(disk);
        auto secrets_owner = std::make_unique<testing::FakeSecretStore>();
        secrets = secrets_owner.get();
        platform.secrets = std::move(secrets_owner);
        auto processes_owner = std::make_unique<testing::ScriptedProcessLauncher>(strand, clock, testing::FakeOs::Windows);
        processes = processes_owner.get();
        platform.processes = std::move(processes_owner);
        auto host_owner = std::make_unique<testing::FakeSessionHost>(strand);
        session_host = host_owner.get();
        platform.session_host = std::move(host_owner);
        auto inspector_owner = std::make_unique<testing::FakePortInspector>();
        port_inspector = inspector_owner.get();
        platform.ports = std::move(inspector_owner);
        platform.peer_inspector = std::make_unique<testing::FakeLoopbackPeerInspector>(false);
        platform.resolver = std::make_unique<testing::FakeResolver>(strand);
        platform.ipc_listener = ipc.make_listener();
        platform.shell = std::make_unique<testing::FakeShell>();
        platform.integration = std::make_unique<testing::FakeRegistrar>();
        platform.security = std::make_unique<testing::FakeSecurityProbe>();
        platform.prerequisites = std::make_unique<testing::FakePrereqs>();
        auto system_owner = std::make_unique<testing::FakeSystemInfo>();
        system = system_owner.get();
        platform.system = std::move(system_owner);
        platform.updater = std::make_unique<testing::FakeUpdateApplier>(true);
        platform.random = std::make_unique<testing::FakeRandom>(7);

        root = DataRoot{paths->default_data_root(), false};
        layout.emplace(root, *paths);
        install = locate_install(*paths, std::nullopt);
        info.app_version = SemVer{1, 0, 0, ""};
        info.epoch = EngineEpoch{77};
        info.self.pid = 4242;
        info.self.image_path = paths->exe_dir() / "reboot-engine";
        info.canonical_root = canonical_root(root);
        info.root_hash16 = root_hash16(info.canonical_root);
        info.endpoint = "engine-" + info.root_hash16;
        info.os = system->os();
    }

    ~EngineRig() {
        if (services && !exit) {
            static_cast<void>(services->lifecycle().shutdown(ShutdownWhen::Now));
            static_cast<void>(strand.pump_until([this] { return exit.has_value(); }, std::chrono::minutes{2},
                                                std::chrono::seconds{20}));
        }
        // As EngineHost: the threads stop before the services go, since queued jobs still run.
        work.reset();
        io.stop();
        if (io_thread.joinable()) io_thread.join();
        workers.shutdown();
        services.reset();
    }

    EngineRig(const EngineRig&) = delete;
    EngineRig& operator=(const EngineRig&) = delete;

    // Startup steps 4 to 11, as EngineHost runs them.
    void boot(EngineOrigin origin = EngineOrigin::Foreground, EngineOverrides overrides = {}) {
        info.origin = origin;
        Result<storage::DataRootReport> prepared = storage::prepare_data_root(*layout, install, *paths, *fs);
        REQUIRE(prepared);
        if (!overrides.upnp) overrides.upnp = std::make_unique<AbsentGateway>(net::MappingMethod::Upnp);
        if (!overrides.natpmp) overrides.natpmp = std::make_unique<AbsentGateway>(net::MappingMethod::NatPmp);
        services = std::make_unique<EngineServices>(EngineRuntime{
            .platform = platform,
            .http = http,
            .quic = quic,
            .clock = clock,
            .strand = strand,
            .timers = timers,
            .workers = workers,
            .io = io,
            .log_ring = log_ring,
            .session_log = nullptr,
            .layout = *layout,
            .install = install,
            .info = info,
            .user_environment = {},
            .uid = std::nullopt,
            .on_exit = [this](EngineExit code) { exit = code; },
            .overrides = std::move(overrides)});
        static_cast<void>(services->open_stores(std::nullopt));
        static_cast<void>(services->reap_orphans());
        services->load_components_and_catalog();
        services->reconcile_integration();
        REQUIRE(services->open_endpoint());
        Result<updates::StartupResume> resume = services->begin_resume();
        REQUIRE(resume);
        bool started = false;
        strand.post([this, &started, startup = std::move(*resume)] {
            services->start();
            services->apply_resume(startup, Result<void>{});
            REQUIRE(services->write_runtime());
            started = true;
        });
        strand.run_until([&started] { return started; });
        settle();
    }

    // Runs what is ready and what the worker and I/O threads post shortly after, and what busy workers post when done.
    void settle() {
        static_cast<void>(strand.pump_until([] { return false; }, std::chrono::seconds{0}, std::chrono::milliseconds{200}));
        static_cast<void>(strand.try_run_until([this] { return workers.idle() && strand.run_ready() == 0; }));
    }

    [[nodiscard]] std::unique_ptr<testing::ApiTestClient> connect(contracts::ipc::CallerContext caller = default_caller(),
                                                                 std::string build = std::string(VersionStreams::ipc_build)) {
        Result<std::unique_ptr<ports::IByteStream>> stream = ipc.make_connector()->connect(info.endpoint, std::chrono::seconds{1});
        REQUIRE(stream);
        auto client = std::make_unique<testing::ApiTestClient>(std::move(*stream));
        client->hello(contracts::ipc::ClientKind::Test, std::move(build), std::move(caller));
        strand.run_until([&client] { return client->hello_ack().has_value(); });
        return client;
    }

    [[nodiscard]] static contracts::ipc::CallerContext default_caller() {
        contracts::ipc::CallerContext caller;
        caller.os_session = "1";
        // On Linux a client shares the engine's desktop by its display, which play and the shell check.
#if defined(__linux__)
        caller.display_env = {{"DISPLAY", ":0"}};
#endif
        return caller;
    }

    template <class Response, class Request>
    [[nodiscard]] Result<Response> call(testing::ApiTestClient& client, u32 method, const Request& request) {
        const u64 id = client.call(method, request);
        REQUIRE(strand.pump_until([&] { return client.reply(id).has_value(); }));
        return *client.template response<Response>(id);
    }

    // The started op's id, or the refusal.
    template <class Request>
    [[nodiscard]] Result<u64> start(testing::ApiTestClient& client, u32 method, const Request& request,
                                    std::optional<bool> detached = std::nullopt) {
        const u64 id = client.start(method, request, detached);
        REQUIRE(strand.pump_until([&] { return client.started_op(id).has_value() || client.reply(id).has_value(); }));
        if (const std::optional<u64> op = client.started_op(id)) return *op;
        return std::unexpected(client.reply_payload(id)->error());
    }

    [[nodiscard]] api::Outcome outcome(testing::ApiTestClient& client, u64 op,
                                       std::chrono::steady_clock::duration simulated = std::chrono::minutes{30}) {
        REQUIRE(strand.pump_until([&] { return client.outcome(op).has_value(); }, simulated));
        return *client.outcome(op);
    }

    template <class Response>
    [[nodiscard]] static Response completed(const api::Outcome& outcome) {
        if (outcome.failed) FAIL(outcome.failed->id);
        if (outcome.timed_out) FAIL("timed out in " << *outcome.timed_out);
        if (outcome.cancelled) FAIL("cancelled");
        REQUIRE(outcome.completed);
        auto decoded = api::decode<Response>(*outcome.completed);
        REQUIRE(decoded);
        return *decoded;
    }

    // Waits for a pending request of `kind` and answers it.
    void answer(testing::ApiTestClient& client, api::UserRequestKind kind, api::RequestAnswer answer) {
        std::optional<u64> request;
        REQUIRE(strand.pump_until([&] {
            Result<api::RequestsPendingResponse> pending =
                call<api::RequestsPendingResponse>(client, api::kRequestsPending, api::RequestsPendingRequest{});
            if (!pending) return false;
            for (const api::UserActionRequired& entry : pending->requests)
                if (entry.kind == kind) request = entry.request_id;
            return request.has_value();
        }));
        api::RequestsRespondRequest respond{*request, std::move(answer)};
        REQUIRE(call<api::RequestsRespondResponse>(client, api::kRequestsRespond, respond));
    }

    // Every reboot-backend spawn runs a conforming FakeBackend.
    void serve_backend() {
        processes->serve_exe(install.backend_exe.filename().string(), [this](const ports::ProcessLaunch&) {
            ++backends_spawned;
            return std::unique_ptr<testing::IStdioPeer>(std::make_unique<testing::FakeBackend>(strand, clock, testing::FakeBackendScript{}));
        });
    }

    // The bundled game server: a real file to hash, whose runs are FakeGameServers that own the
    // ports they report.
    void serve_game_server() {
        write_file(install.game_server_exe, "game server v1");
        processes->on_exe(install.game_server_exe.filename().string(), [this](testing::ScriptedChild& child) -> Result<void> {
            testing::FakeGameServerScript script{.bind_sockets = false};
            script.description.capabilities.needs_backend = true;
            script.description.capabilities.operator_commands = {"run_command"};
            if (child.launch().args == std::vector<std::string>{"--describe"}) {
                child.write_stdout(testing::describe_frame(script));
                child.exit(ports::ChildExit{0, std::nullopt});
                return {};
            }
            auto fake = std::make_unique<testing::FakeGameServer>(strand, clock, std::move(script));
            game_servers.push_back(fake.get());
            fake->on_listening([this, pid = child.pid()](const contracts::game_server::Listening& listening) {
                for (const contracts::game_server::BoundSocket& bound : listening.bound)
                    port_inspector->set_udp_owner(Port{bound.port}, ports::PortOwner{.pid = pid});
            });
            child.attach_peer(std::move(fake));
            return {};
        });
    }

    // From now on, requests of these kinds are accepted as they appear, while the test pumps.
    void auto_accept(testing::ApiTestClient& client, std::vector<api::UserRequestKind> kinds) {
        accepting = &client;
        accept_kinds = std::move(kinds);
        accept_sub = client.subscribe(api::EventFilter{{api::EventKind::UserActionRequired}, std::nullopt, std::nullopt});
        client.credit(accept_sub, 1000);
    }

    // The op's outcome, accepting requests as auto_accept set up.
    [[nodiscard]] api::Outcome settle_op(testing::ApiTestClient& client, u64 op,
                                         std::chrono::steady_clock::duration simulated = std::chrono::minutes{30}) {
        REQUIRE(strand.pump_until([&] {
            if (client.outcome(op)) return true;
            accept_requests();
            return false;
        }, simulated));
        return *client.outcome(op);
    }

    void accept_requests() {
        if (accepting == nullptr || accepting->events(accept_sub).size() == accept_seen) return;
        accept_seen = accepting->events(accept_sub).size();
        Result<api::RequestsPendingResponse> pending =
            call<api::RequestsPendingResponse>(*accepting, api::kRequestsPending, api::RequestsPendingRequest{});
        REQUIRE(pending);
        for (const api::UserActionRequired& entry : pending->requests) {
            if (std::ranges::find(accept_kinds, entry.kind) == accept_kinds.end()) continue;
            accepted.push_back(entry.kind);
            api::RequestsRespondRequest respond{entry.request_id, accept()};
            REQUIRE(call<api::RequestsRespondResponse>(*accepting, api::kRequestsRespond, respond));
        }
    }

    // A complete profile: every oneof set, hosting a version without a build.
    [[nodiscard]] static api::HostProfile arena_profile() {
        api::HostProfile profile;
        profile.name = "Arena";
        profile.server_name = "Arena server";
        profile.max_players = 20;
        profile.playlist = "Playlist_DefaultSolo";
        profile.tick_rate = 30;
        profile.listing = api::Listing::Unlisted;
        profile.port.range = api::PortRange{47000, 47100};
        profile.start.manual = true;
        profile.version = api::HostVersion{api::GameVersion{14, 40, std::nullopt}, 14550713};
        return profile;
    }

    [[nodiscard]] static api::RequestAnswer accept() {
        api::RequestAnswer out;
        out.decision = api::Decision{true, false};
        return out;
    }

    ManualClock clock;
    TestStrand strand;
    TimerService timers;
    WorkerPool workers{3};
    boost::asio::io_context io;
    std::optional<boost::asio::executor_work_guard<boost::asio::io_context::executor_type>> work;
    std::thread io_thread;
    std::optional<testing::ScratchDir> scratch;

    testing::InMemoryIpc ipc;
    testing::FakeHttpTransport http;
    testing::FakeQuicTransport quic;
    logging::LogRing log_ring;
    ports::PlatformServices platform;
    testing::FakePlatformPaths* paths = nullptr;
    DiskFileSystem* fs = nullptr;
    testing::FakeSecretStore* secrets = nullptr;
    testing::ScriptedProcessLauncher* processes = nullptr;
    testing::FakeSessionHost* session_host = nullptr;
    testing::FakePortInspector* port_inspector = nullptr;
    testing::FakeSystemInfo* system = nullptr;

    DataRoot root;
    std::optional<AppLayout> layout;
    InstallLayout install;
    EngineInfo info;
    std::optional<EngineExit> exit;
    std::unique_ptr<EngineServices> services;

    int backends_spawned = 0;
    // Owned by their ScriptedChild, so valid only while it runs.
    std::vector<testing::FakeGameServer*> game_servers;
    testing::ApiTestClient* accepting = nullptr;
    std::vector<api::UserRequestKind> accept_kinds;
    u64 accept_sub = 0;
    std::size_t accept_seen = 0;
    std::vector<api::UserRequestKind> accepted;
};

}  // namespace rb::engine::test
