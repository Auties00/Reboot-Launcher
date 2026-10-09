#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <any>
#include <chrono>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <boost/asio/io_context.hpp>
#include <boost/asio/ip/udp.hpp>

#include "host_test_support.hpp"
#include "reboot/builds/cl_table.hpp"
#include "reboot/builds/library.hpp"
#include "reboot/catalog/catalog_service.hpp"
#include "reboot/catalog/catalog_source.hpp"
#include "reboot/foundation/events.hpp"
#include "reboot/foundation/operation.hpp"
#include "reboot/foundation/paths.hpp"
#include "reboot/foundation/user_request.hpp"
#include "reboot/gameserver/game_server_binary.hpp"
#include "reboot/host/host_backend_link.hpp"
#include "reboot/host/host_port_allocator.hpp"
#include "reboot/host/host_profile_store.hpp"
#include "reboot/host/host_profiles_changed.hpp"
#include "reboot/host/host_service.hpp"
#include "reboot/host/untested_host_prompt.hpp"
#include "reboot/identity/identity_service.hpp"
#include "reboot/net/datagram_connector.hpp"
#include "reboot/net/port_mapper_service.hpp"
#include "reboot/net/port_mapping_gateway.hpp"
#include "reboot/net/port_owner_service.hpp"
#include "reboot/net/port_preflight.hpp"
#include "reboot/net/udp_beacon_prober.hpp"
#include "reboot/publish/host_identity_store.hpp"
#include "reboot/publish/host_publisher.hpp"
#include "reboot/publish/publish_notice_sink.hpp"
#include "reboot/sessions/session_driver.hpp"
#include "reboot/sessions/session_event.hpp"
#include "reboot/sessions/session_registry.hpp"
#include "reboot/storage/settings.hpp"
#include "reboot/storage/settings_registry.hpp"
#include "reboot/support/support_policy.hpp"
#include "reboot/testing/event_recorder.hpp"
#include "reboot/testing/fake_game_server.hpp"
#include "reboot/testing/fake_platform_paths.hpp"
#include "reboot/testing/fake_port_inspector.hpp"
#include "reboot/testing/fake_quic_transport.hpp"
#include "reboot/testing/fake_random.hpp"
#include "reboot/testing/fake_shell.hpp"
#include "reboot/testing/in_memory_file_system.hpp"
#include "reboot/testing/scripted_process_launcher.hpp"

using namespace reboot;
using namespace reboot::host;
using namespace std::chrono_literals;
namespace asio = boost::asio;
namespace gs = reboot::contracts::game_server;

namespace {

constexpr std::string_view kExeName = "reboot-game-server";

class NullCatalogSource final : public catalog::ICatalogSource {
public:
    void load(catalog::CatalogFetch, CancelToken, UniqueFunction<void(catalog::CatalogLoadResult)>) override {
        FAIL("the host tests never refresh the catalog");
    }
};

// Answers the loopback probe while `answer` is set; replies arrive on the strand like I/O-thread posts.
class LoopbackConnector final : public net::IDatagramConnector {
public:
    explicit LoopbackConnector(Executor& io) : io_(io) {}

    Result<std::unique_ptr<net::IDatagramChannel>> connect(Endpoint target, net::DatagramCallbacks callbacks) override {
        targets.push_back(target);
        auto state = std::make_shared<State>();
        state->callbacks = std::move(callbacks);
        return std::make_unique<Channel>(*this, std::move(state));
    }

    bool answer = true;
    std::vector<Endpoint> targets;

private:
    struct State {
        net::DatagramCallbacks callbacks;
        bool open = true;
    };

    class Channel final : public net::IDatagramChannel {
    public:
        Channel(LoopbackConnector& owner, std::shared_ptr<State> state) : owner_(owner), state_(std::move(state)) {}
        ~Channel() override { state_->open = false; }
        Channel(const Channel&) = delete;
        Channel& operator=(const Channel&) = delete;

        void send(std::span<const u8>) override {
            if (!owner_.answer) return;
            owner_.io_.post([state = state_] {
                if (state->open) state->callbacks.on_datagram(std::span<const u8>(gs::kRbsbProbe));
            });
        }

    private:
        LoopbackConnector& owner_;
        std::shared_ptr<State> state_;
    };

    Executor& io_;
};

// A router that maps every internal port to internal + 1000.
class FakeGateway final : public net::IPortMappingGateway {
public:
    FakeGateway(net::MappingMethod method, bool present) : method_(method), present_(present) {}

    [[nodiscard]] net::MappingMethod method() const noexcept override { return method_; }
    std::expected<net::GatewayInfo, net::GatewayError> discover(std::chrono::milliseconds, const CancelToken&) override {
        if (!present_) return std::unexpected(net::GatewayError{net::GatewayErrorCode::NoGateway});
        return net::GatewayInfo{IpAddress::v4(0xC0A80105), IpAddress::v4(0x01020304)};
    }
    std::expected<net::PortMapping, net::GatewayError> add(const net::GatewayMappingRequest& request,
                                                           std::chrono::milliseconds) override {
        return net::PortMapping{request.internal, Port{static_cast<u16>(request.internal.value + 1000)}, method_,
                                request.lease, request.lan_address};
    }
    std::expected<void, net::GatewayError> remove(const net::PortMapping&, std::chrono::milliseconds) override { return {}; }
    std::expected<std::vector<net::GatewayEntry>, net::GatewayError> list(std::chrono::milliseconds) override {
        return std::vector<net::GatewayEntry>{};
    }

private:
    net::MappingMethod method_;
    bool present_;
};

class FakeBackendLink final : public IHostBackendLink {
public:
    explicit FakeBackendLink(Executor& strand) : strand_(strand) {}

    void acquire(SessionId, std::string account_id, CancelToken,
                 UniqueFunction<void(Result<gameserver::BackendAccess>)> done) override {
        accounts.push_back(account_id);
        if (error) {
            strand_.post([done = std::move(done), error = *error]() mutable { done(std::unexpected(error)); });
            return;
        }
        strand_.post([done = std::move(done), account_id = std::move(account_id)]() mutable {
            done(gameserver::BackendAccess{"http://127.0.0.1:9/s/key/", account_id, SecretString{std::string("service")}});
        });
    }
    void release(SessionId session) override { released.push_back(session); }

    std::optional<Diagnostic> error;
    std::vector<std::string> accounts;
    std::vector<SessionId> released;

private:
    Executor& strand_;
};

class NoNotices final : public publish::IPublishNoticeSink {
public:
    void on_publish_notice(const publish::PublishNotice&) override {}
};

class PlayDriver final : public sessions::ISessionDriver {
public:
    void stop(const sessions::StopRequest&, sessions::StopDone done) override { done(Result<void>{}); }
};

asio::ip::udp::socket bind_with_room(asio::io_context& io) {
    while (true) {
        asio::ip::udp::socket socket(io, asio::ip::udp::endpoint(asio::ip::udp::v4(), 0));
        if (socket.local_endpoint().port() <= 65000) return socket;
    }
}

struct Fixture {
    explicit Fixture(HostServiceOptions options = {}) {
        fs.write_text(exe, "server v1");
        static_cast<void>(describe_cache.load());
        static_cast<void>(profiles_document.load());
        static_cast<void>(settings_document.load());
        static_cast<void>(accounts.load());
        static_cast<void>(logins.load());
        static_cast<void>(library_document.load());
        identity.ensure_records();
        identities.load_memory_only();
        REQUIRE(profiles.ensure_builtin(HostListing::Unlisted));
        launcher.on_exe(kExeName, [this](testing::ScriptedChild& child) -> Result<void> { return on_spawn(child); });
        make(std::move(options));
    }

    ~Fixture() {
        service.reset();
        workers.shutdown();
    }
    Fixture(const Fixture&) = delete;
    Fixture& operator=(const Fixture&) = delete;

    void make(HostServiceOptions options) {
        service = std::make_unique<HostService>(HostServiceDeps{
            .profiles = profiles,
            .sessions = registry,
            .binary = binary,
            .support = support,
            .library = library,
            .identity = identity,
            .settings = settings,
            .allocator = allocator,
            .port_owners = owners,
            .prober = prober,
            .mapper = mapper,
            .publisher = publisher,
            .identities = identities,
            .backend = backend,
            .launcher = launcher,
            .fs = fs,
            .workers = workers,
            .strand = strand,
            .timers = timers,
            .clock = clock,
            .ops = ops,
            .events = events,
            .requests = requests,
            .layout = layout,
            .join_password = [this](const HostProfileId&) -> Result<std::optional<SecretString>> {
                if (password_error) return std::unexpected(*password_error);
                if (!password) return std::optional<SecretString>();
                return std::optional<SecretString>(SecretString{*password});
            },
            .base_env = []() -> Result<process::BuiltEnv> { return process::BuiltEnv{}; },
            .record = [this](const process::ChildRecord& child, process::RecordChange change) {
                records.emplace_back(child, change);
            },
            .options = std::move(options)});
    }

    Result<void> on_spawn(testing::ScriptedChild& child) {
        if (child.launch().args == std::vector<std::string>{"--describe"}) {
            child.write_stdout(testing::describe_frame(script));
            child.exit(ports::ChildExit{0, std::nullopt});
            return {};
        }
        testing::FakeGameServerScript spawned = script;
        if (bind_failures > 0) {
            --bind_failures;
            spawned.bind_failure_index = 0;
        }
        auto fake = std::make_unique<testing::FakeGameServer>(strand, clock, std::move(spawned));
        fakes.push_back(fake.get());
        if (own_ports)
            fake->on_listening([this, pid = child.pid()](const gs::Listening& listening) {
                for (const gs::BoundSocket& bound : listening.bound)
                    inspector.set_udp_owner(Port{bound.port}, ports::PortOwner{.pid = pid});
            });
        child.attach_peer(std::move(fake));
        return {};
    }

    [[nodiscard]] Port above(u16 offset) const { return Port{static_cast<u16>(held.value + offset)}; }

    // A profile on free ports above the held one, without port mapping unless asked.
    HostProfile profile(std::string name, UniqueFunction<void(HostProfile&)> edit = {}) {
        HostProfile draft = new_profile(HostProfileId{}, std::move(name), HostListing::Listed);
        draft.version = HostVersion{*GameVersion::parse("14.40"), Changelist{14550713}};
        draft.port = AutoPorts{PortRange{above(1), above(20)}};
        draft.port_mapping = false;
        if (edit) edit(draft);
        Result<HostProfile> created = service->create_profile(std::move(draft));
        REQUIRE(created);
        return *created;
    }

    void use_auto_profile_ports() {
        HostProfile automatic = *profiles.get(kAutoProfileId);
        automatic.port = AutoPorts{PortRange{above(1), above(20)}};
        automatic.version = HostVersion{*GameVersion::parse("14.40"), Changelist{14550713}};
        REQUIRE(service->update_profile(std::move(automatic)));
    }

    [[nodiscard]] HostStartRequest request(const HostProfile& profile) const {
        HostStartRequest request;
        request.profile = profile.id;
        request.untested_confirmed = true;
        return request;
    }

    [[nodiscard]] OpHandle start(HostStartRequest request) {
        Result<OpHandle> started = service->start(std::move(request));
        if (!started) FAIL(test::describe(started.error()));
        return *started;
    }

    [[nodiscard]] ErasedOutcome outcome(OpHandle op) {
        strand.run_until([&] { return ops.outcome(op.id()).has_value(); });
        return *ops.outcome(op.id());
    }

    [[nodiscard]] SessionId started(OpHandle op) {
        const ErasedOutcome done = outcome(op);
        if (const auto* failed = std::get_if<Failed>(&done)) FAIL(test::describe(failed->error));
        const auto* completed = std::get_if<Completed<std::any>>(&done);
        REQUIRE(completed != nullptr);
        return std::any_cast<SessionId>(completed->value);
    }

    [[nodiscard]] Diagnostic failed(OpHandle op) {
        const ErasedOutcome done = outcome(op);
        const auto* failure = std::get_if<Failed>(&done);
        REQUIRE(failure != nullptr);
        return failure->error;
    }

    [[nodiscard]] SessionId live(const HostProfile& profile) {
        const SessionId session = started(start(request(profile)));
        run_until_phase(session, HostPhase::Live);
        return session;
    }

    void run_until_phase(const SessionId& session, HostPhase phase) {
        strand.run_until([&] {
            const Result<HostPhase> now = service->phase(session);
            return now && *now == phase;
        });
    }

    void run_until_ended(const SessionId& session) {
        strand.run_until([&] { return !registry.get(session).has_value(); });
        strand.run_ready();
    }

    [[nodiscard]] std::vector<testing::ScriptedChild*> servers() const {
        std::vector<testing::ScriptedChild*> out;
        for (testing::ScriptedChild* child : launcher.children())
            if (child->launch().args == std::vector<std::string>{"--control=stdio"}) out.push_back(child);
        return out;
    }

    [[nodiscard]] std::optional<sessions::SessionEnded> ended(const SessionId& session) {
        recorder.pump();
        for (const sessions::SessionEnded* end : recorder.payloads<sessions::SessionEnded>(EventKind::SessionEnded))
            if (end->session == session) return *end;
        return std::nullopt;
    }

    template <class E>
    [[nodiscard]] std::vector<E> events_of(EventKind kind) {
        recorder.pump();
        std::vector<E> out;
        for (const E* payload : recorder.payloads<E>(kind)) out.push_back(*payload);
        return out;
    }

    [[nodiscard]] std::size_t frames_of(const testing::ScriptedChild& child, u64 type) const {
        return child.stdin_frames().count(type);
    }

    ManualClock clock;
    test::TestStrand strand{clock};
    TimerService timers{clock, strand};
    EventBus events{EngineEpoch{1}};
    OpRegistry ops{clock, timers, events};
    UserRequestRegistry requests{events};
    testing::EventRecorder recorder{events};
    testing::InMemoryFileSystem fs;
    testing::FakeRandom random{3};
    testing::ScriptedProcessLauncher launcher{strand, clock, testing::FakeOs::Windows};
    testing::FakePortInspector inspector;
    asio::io_context io;
    asio::ip::udp::socket holder = bind_with_room(io);
    Port held{holder.local_endpoint().port()};
    testing::FakePlatformPaths paths;
    AppLayout layout{DataRoot{testing::default_fake_root() / "data", true}, paths};
    NativePath exe = testing::default_fake_root() / "app" / std::string(kExeName);
    WorkerPool workers{2};

    storage::DocumentStore<HostProfilesDocument> profiles_document{fs, workers, strand, clock, layout.host_profiles_file()};
    storage::DocumentStore<storage::SettingsDocument> settings_document{fs, workers, strand, clock, layout.settings_file()};
    storage::DocumentStore<storage::AccountsDocument> accounts{fs, workers, strand, clock, layout.accounts_file()};
    storage::DocumentStore<identity::BackendLoginsDocument> logins{fs, workers, strand, clock,
                                                                   testing::default_fake_root() / "data" / "backend-logins.json"};
    storage::DocumentStore<storage::LibraryDocument> library_document{fs, workers, strand, clock, layout.library_file()};
    storage::DocumentStore<gameserver::DescribeCacheDocument> describe_cache{fs, workers, strand, clock, layout.describe_cache()};

    HostProfileStore profiles{profiles_document, random};
    storage::SettingsRegistry settings_registry;
    storage::Settings settings{settings_document, settings_registry, events};
    identity::IdentityService identity{accounts, logins, random, events};
    sessions::SessionRegistry registry{clock, random, strand, timers, events};
    NullCatalogSource catalog_source;
    catalog::CatalogService catalog{catalog_source, catalog_source, ops, events, clock};
    builds::ClTable cl_table;
    testing::FakeShell shell;
    InstallLayout install{.install_dir = testing::default_fake_root() / "app"};
    builds::Library library{builds::LibraryDeps{.store = library_document,
                                                .usage = registry,
                                                .cl_table = cl_table,
                                                .catalog = catalog,
                                                .fs = fs,
                                                .shell = shell,
                                                .workers = workers,
                                                .strand = strand,
                                                .ops = ops,
                                                .events = events,
                                                .clock = clock,
                                                .random = random,
                                                .install = install}};
    support::SupportPolicy support{support::SupportInputs{}};
    std::vector<std::pair<process::ChildRecord, process::RecordChange>> records;
    gameserver::GameServerBinary binary{exe,     fs,    launcher, workers, strand, timers, clock, describe_cache,
                                        []() -> Result<process::BuiltEnv> { return process::BuiltEnv{}; },
                                        nullptr};
    net::PortOwnerService owners{inspector, launcher};
    net::PortPreflight preflight{io, owners};
    HostPortAllocator allocator{preflight, workers, strand};
    LoopbackConnector connector{strand};
    net::UdpBeaconProber prober{connector, strand, timers, clock};
    FakeGateway upnp{net::MappingMethod::Upnp, true};
    FakeGateway natpmp{net::MappingMethod::NatPmp, false};
    net::PortMapperService mapper{upnp, natpmp, "00112233aabbccdd", {}, [](std::vector<net::MappingRecord>) {},
                                  workers, strand, timers, events};
    testing::FakeQuicTransport quic{strand};
    publish::HostIdentityStore identities{fs, workers, strand, random, ops, layout.host_identity_dir()};
    NoNotices notices;
    publish::HostPublisher publisher{quic,   identities, notices, strand, timers, random, events,
                                     browser::RbsbEndpoint{"edge.test", Port{443}, std::nullopt, browser::EndpointSource::Compiled}};
    FakeBackendLink backend{strand};

    testing::FakeGameServerScript script{.bind_sockets = false};
    int bind_failures = 0;
    bool own_ports = true;
    std::vector<testing::FakeGameServer*> fakes;
    std::optional<std::string> password;
    std::optional<Diagnostic> password_error;
    std::unique_ptr<HostService> service;
};

gs::MatchEnded match_ended() { return gs::MatchEnded{gs::MatchEndReason::Completed, std::string("Winner-aaaaaa"), {}}; }

}  // namespace

TEST_CASE("a start runs the server through Listening to a published Live session", "[host][service]") {
    Fixture f;
    f.password = "letmein";
    const HostProfile profile = f.profile("Weekend");
    const OpHandle op = f.start(f.request(profile));
    const SessionId session = f.started(op);

    const std::vector<HostListening> listening = f.events_of<HostListening>(EventKind::HostListening);
    REQUIRE(listening.size() == 1);
    CHECK(listening[0].session == session);
    CHECK(listening[0].block == PortBlock{f.above(1), 1});
    REQUIRE(listening[0].bound.size() == 1);
    CHECK(f.service->listening(session)->block == listening[0].block);

    f.run_until_phase(session, HostPhase::Live);
    const std::optional<publish::PublishState> published = f.publisher.state(session);
    REQUIRE(published);
    CHECK_FALSE(published->hidden);
    CHECK(published->advertised_port == f.above(1));
    REQUIRE(f.connector.targets.size() == 1);
    CHECK(f.connector.targets[0] == Endpoint{IpAddress::v4(0x7F000001), f.above(1)});

    const sessions::SessionInfo info = *f.registry.get(session);
    CHECK(info.kind == sessions::SessionKind::Host);
    CHECK(info.phase == sessions::SessionPhase::Running);
    CHECK(info.profile == profile.id);
    CHECK(info.pinned.game_server_sha256.has_value());
    CHECK(info.label == "Weekend");

    REQUIRE(f.servers().size() == 1);
    const auto welcome = f.servers()[0]->stdin_frames().last<gs::ServerWelcome>();
    REQUIRE(welcome);
    REQUIRE(*welcome);
    CHECK((*welcome)->config.listen.ports == std::vector<u16>{f.above(1).value});
    CHECK((*welcome)->config.game.version == "14.40");
    CHECK_FALSE((*welcome)->config.game.build_root.has_value());

    const HostSnapshot snapshot = *f.service->status(session);
    CHECK(snapshot.phase.phase == HostPhase::Live);
    CHECK(snapshot.listening.has_value());
    CHECK(snapshot.publish.has_value());
    CHECK(f.service->share_link(session)->server == published->server);
    CHECK(f.records.size() >= 1);
}

TEST_CASE("start checks the profile, the link, the limit and the target first", "[host][service]") {
    HostServiceOptions options;
    options.host_limit = 1;
    Fixture f(options);
    const HostProfile weekend = f.profile("Weekend");

    HostStartRequest unknown;
    unknown.profile = test::profile_id(9);
    CHECK(f.service->start(unknown).error().id == "host.profile_not_found");
    HostStartRequest automatic;
    automatic.profile = kAutoProfileId;
    CHECK(f.service->start(automatic).error().id == "host.auto_profile_needs_link");
    HostStartRequest linked = f.request(weekend);
    linked.linked_to = SessionId{test::profile_id(4).value};
    CHECK(f.service->start(linked).error().id == "host.linked_needs_auto_profile");
    HostStartRequest bad_port = f.request(weekend);
    bad_port.overrides.port = Port{80};
    CHECK(f.service->start(bad_port).error().id == "host.invalid_port_policy");
    HostStartRequest no_target;
    no_target.profile = kDefaultProfileId;
    CHECK(f.service->start(no_target).error().id == "host.no_build_selected");

    const Result<builds::InstalledBuild> unconfirmed =
        f.library.add(builds::NewBuild{.name = "Mystery", .root = testing::default_fake_root() / "builds" / "mystery"});
    REQUIRE(unconfirmed);
    HostStartRequest unknown_version = f.request(weekend);
    unknown_version.overrides.build = unconfirmed->id;
    CHECK(f.service->start(unknown_version).error().id == "host.build_version_unknown");

    const SessionId session = f.started(f.start(f.request(weekend)));
    CHECK(f.service->start(f.request(weekend)).error().id == "host.profile_busy");
    const HostProfile other = f.profile("Other");
    const Result<OpHandle> limited = f.service->start(f.request(other));
    REQUIRE_FALSE(limited);
    CHECK(limited.error().id == "host.host_limit_reached");
    CHECK(f.service->delete_profile(weekend.id).error().id == "host.profile_busy");

    REQUIRE(f.registry.stop(session, sessions::StopRequest{}, nullptr));
    f.run_until_ended(session);
    CHECK(f.service->status(session).error().id == "host.not_host_session");
    REQUIRE(f.service->delete_profile(weekend.id));
}

TEST_CASE("an installed build hosts with its root and pins the build", "[host][service]") {
    Fixture f;
    const NativePath root = testing::default_fake_root() / "builds" / "14.40";
    const Result<builds::InstalledBuild> build = f.library.add(builds::NewBuild{
        .name = "Season 14", .root = root, .version = builds::DetectedVersion{.version = *GameVersion::parse("14.40"), .cl = Changelist{7}}});
    REQUIRE(build);
    REQUIRE(f.library.select(support::SupportRole::Host, build->id));
    const HostProfile profile = f.profile("Selected", [](HostProfile& draft) { draft.version.reset(); });
    const SessionId session = f.started(f.start(f.request(profile)));
    CHECK(f.registry.get(session)->pinned.build == build->id);
    const auto welcome = f.servers()[0]->stdin_frames().last<gs::ServerWelcome>();
    REQUIRE((*welcome)->config.game.build_root.has_value());
    CHECK((*welcome)->config.game.cl == 7u);
}

TEST_CASE("an untested build asks first and a refusal opens nothing", "[host][service]") {
    Fixture f;
    const HostProfile profile = f.profile("Untested");
    HostStartRequest ask = f.request(profile);
    ask.untested_confirmed = false;
    const OpHandle op = f.start(ask);
    f.strand.run_until([&] { return !f.requests.pending().empty(); });
    const UserRequest prompt = f.requests.pending().front();
    CHECK(prompt.kind == UserRequestKind::ConfirmUntested);
    CHECK(std::any_cast<UntestedHostPrompt>(prompt.payload).profile == profile.id);
    CHECK(f.requests.respond(prompt.id, std::any(std::string("yes"))).error().id == "host.invalid_answer");
    REQUIRE(f.requests.respond(prompt.id, std::any(false)));
    const Diagnostic refused = f.failed(op);
    CHECK(refused.id == "host.untested_declined");
    CHECK(refused.kind == ErrorKind::Cancelled);
    CHECK(f.registry.list().empty());

    const OpHandle again = f.start(ask);
    f.strand.run_until([&] { return !f.requests.pending().empty(); });
    REQUIRE(f.requests.respond(f.requests.pending().front().id, std::any(true)));
    CHECK(f.registry.get(f.started(again)));
}

TEST_CASE("a build outside the described range is blocked", "[host][service]") {
    Fixture f;
    f.script.description.supports = {gs::VersionSupport{"3.5", "4.0", {}}};
    const HostProfile profile = f.profile("Old");
    const Diagnostic blocked = f.failed(f.start(f.request(profile)));
    CHECK(blocked.domain == ErrorDomain::Support);
    CHECK(f.registry.list().empty());
    CHECK(f.servers().empty());
}

TEST_CASE("a server that never listens fails the start with a listen timeout", "[host][service]") {
    Fixture f;
    f.script.listen_delay = 300s;
    const HostProfile profile = f.profile("Slow");
    const OpHandle op = f.start(f.request(profile));
    f.strand.run_until([&] { return f.servers().size() == 1; });
    f.strand.advance(121s);
    const Diagnostic timeout = f.failed(op);
    CHECK(timeout.id == "host.listen_timeout");
    f.strand.advance(10s);
    REQUIRE(f.registry.list().empty());
    const std::vector<sessions::SessionEnded> ended = f.events_of<sessions::SessionEnded>(EventKind::SessionEnded);
    REQUIRE(ended.size() == 1);
    CHECK(ended[0].reason == sessions::StopReason::LaunchFailed);
    CHECK(f.allocator.block(ended[0].session) == std::nullopt);
}

TEST_CASE("Auto moves above a block the server cannot bind; Pinned fails", "[host][service]") {
    Fixture f;
    f.bind_failures = 1;
    const HostProfile automatic = f.profile("Auto block");
    const SessionId session = f.started(f.start(f.request(automatic)));
    REQUIRE(f.servers().size() == 2);
    CHECK(f.service->listening(session)->block == PortBlock{f.above(2), 1});

    f.bind_failures = 1;
    const HostProfile pinned = f.profile("Pinned", [&](HostProfile& draft) { draft.port = PinnedPorts{f.above(10)}; });
    const Diagnostic failed = f.failed(f.start(f.request(pinned)));
    CHECK(failed.id == "host.listen_failed");
    f.strand.advance(10s);
    CHECK(f.registry.list().size() == 1);
}

TEST_CASE("readiness that misses the deadline leaves the server up unpublished until it answers", "[host][service]") {
    Fixture f;
    f.connector.answer = false;
    const HostProfile profile = f.profile("Quiet");
    const SessionId session = f.started(f.start(f.request(profile)));
    CHECK(*f.service->phase(session) == HostPhase::WaitingForReadiness);
    f.strand.advance(121s);
    CHECK(*f.service->phase(session) == HostPhase::LiveUnpublished);
    const HostSnapshot snapshot = *f.service->status(session);
    REQUIRE(snapshot.phase.reason);
    CHECK(snapshot.phase.reason->id == "host.readiness_timeout");
    CHECK_FALSE(f.publisher.state(session));
    CHECK(f.registry.get(session)->degraded.size() == 1);

    f.connector.answer = true;
    CHECK(f.strand.advance_until([&] { return *f.service->phase(session) == HostPhase::Live; }, 60s));
    CHECK(f.publisher.state(session));
    CHECK(f.registry.get(session)->degraded.empty());
}

TEST_CASE("a port held by another process is reported, or stops the session when asked", "[host][service]") {
    HostServiceOptions options;
    options.readiness.on_timeout = ReadinessTimeoutAction::StopSession;
    Fixture f(options);
    f.own_ports = false;
    const HostProfile profile = f.profile("Squatted");
    const SessionId session = f.started(f.start(f.request(profile)));
    f.strand.advance(121s);
    f.run_until_ended(session);
    const std::optional<sessions::SessionEnded> ended = f.ended(session);
    REQUIRE(ended);
    CHECK(ended->reason == sessions::StopReason::LaunchFailed);
    REQUIRE(ended->error);
    CHECK(ended->error->id == "host.port_not_owned");
}

TEST_CASE("match end restarts in process once, after a cancellable delay", "[host][service]") {
    Fixture f;
    f.script.events = {{1s, match_ended()}, {2s, match_ended()}};
    const HostProfile profile = f.profile("Restart");
    const SessionId session = f.live(profile);
    f.strand.advance(3s);
    CHECK(*f.service->phase(session) == HostPhase::Restarting);
    const std::vector<MatchEvent> ended = f.events_of<MatchEvent>(EventKind::MatchEvent);
    const auto announced = std::ranges::find_if(ended, [](const MatchEvent& event) { return event.action.has_value(); });
    REQUIRE(announced != ended.end());
    CHECK(announced->state == HostMatchState::Ended);
    CHECK(announced->action == MatchEndAction::Restart);
    CHECK(announced->fires_at.has_value());
    CHECK(announced->result->winner == "Winner-aaaaaa");
    CHECK(f.service->command(session, gameserver::RunCommand{"say hi"}, [](gameserver::CommandResult) {}).error().id ==
          "host.server_not_running");

    f.strand.advance(10s);
    f.run_until_phase(session, HostPhase::Live);
    CHECK(f.frames_of(*f.servers()[0], contract_frame_type_v<gs::Reset>) == 1);
    CHECK(f.servers().size() == 1);
    CHECK(f.service->status(session)->match.state == HostMatchState::Lobby);
}

TEST_CASE("cancel_match_end keeps the server in its lobby", "[host][service]") {
    Fixture f;
    f.script.events = {{1s, match_ended()}};
    const HostProfile profile = f.profile("Cancel");
    const SessionId session = f.live(profile);
    CHECK(*f.service->cancel_match_end(session) == false);
    f.strand.advance(2s);
    REQUIRE(*f.service->cancel_match_end(session));
    CHECK(*f.service->phase(session) == HostPhase::Live);
    CHECK(f.service->status(session)->match.state == HostMatchState::Ended);
    CHECK_FALSE(f.service->status(session)->match.action.has_value());
    CHECK(*f.service->cancel_match_end(session) == false);
    f.strand.advance(20s);
    CHECK(f.frames_of(*f.servers()[0], contract_frame_type_v<gs::Reset>) == 0);
}

TEST_CASE("a stop during Restarting never relaunches", "[host][service]") {
    Fixture f;
    f.script.events = {{1s, match_ended()}};
    const HostProfile profile = f.profile("Stop");
    const SessionId session = f.live(profile);
    f.strand.advance(2s);
    REQUIRE(*f.service->phase(session) == HostPhase::Restarting);
    REQUIRE(f.registry.stop(session, sessions::StopRequest{}, nullptr));
    f.run_until_ended(session);
    f.strand.advance(30s);
    CHECK(f.servers().size() == 1);
    CHECK(f.ended(session)->reason == sessions::StopReason::User);
    CHECK(f.frames_of(*f.servers()[0], contract_frame_type_v<gs::Shutdown>) == 1);
}

TEST_CASE("without an in-process reset the server respawns on the same block", "[host][service]") {
    Fixture f;
    f.script.description.capabilities.in_process_reset = false;
    f.script.events = {{1s, match_ended()}};
    const HostProfile profile = f.profile("Respawn");
    const SessionId session = f.live(profile);
    const PortBlock block = f.service->listening(session)->block;
    f.strand.advance(12s);
    f.strand.run_until([&] { return f.servers().size() == 2; });
    f.run_until_phase(session, HostPhase::Live);
    CHECK(f.service->listening(session)->block == block);
    // The respawn goes through Spawning again, but the registry never leaves Running.
    bool running = false;
    for (const sessions::SessionStateChanged& state :
         f.events_of<sessions::SessionStateChanged>(EventKind::SessionStateChanged)) {
        if (state.session != session) continue;
        if (running) CHECK(state.phase == sessions::SessionPhase::Running);
        running = running || state.phase == sessions::SessionPhase::Running;
    }
    CHECK(running);
    CHECK(f.events_of<HostListening>(EventKind::HostListening).size() == 2);
    CHECK(f.frames_of(*f.servers()[0], contract_frame_type_v<gs::Shutdown>) == 1);
    CHECK(f.registry.get(session)->phase == sessions::SessionPhase::Running);
}

TEST_CASE("a match end reported by the server being replaced starts no second restart", "[host][service]") {
    Fixture f;
    f.script.description.capabilities.in_process_reset = false;
    // The old server lives on until its stdin closes after the stop grace.
    f.script.child.reply_delay = 60s;
    f.script.events = {{2s, match_ended()}, {13s, match_ended()}};
    const HostProfile profile = f.profile("Replaced");
    const SessionId session = f.live(profile);
    f.strand.advance(14s);
    REQUIRE(f.servers().size() == 1);
    const std::vector<MatchEvent> events = f.events_of<MatchEvent>(EventKind::MatchEvent);
    CHECK(std::ranges::count_if(events, [](const MatchEvent& event) { return event.action.has_value(); }) == 1);

    f.strand.advance(5s);
    f.strand.run_until([&] { return f.servers().size() == 2; });
    f.run_until_phase(session, HostPhase::Live);
}

TEST_CASE("an update drain right after a match ended stops at once", "[host][service]") {
    Fixture f;
    f.script.events = {{1s, gs::PlayerJoined{1, "Player-aaaaaa", "Player", "203.0.113.9"}}, {2s, match_ended()}};
    const HostProfile profile = f.profile("Ended");
    const SessionId session = f.live(profile);
    f.strand.advance(3s);
    REQUIRE(*f.service->phase(session) == HostPhase::Restarting);
    REQUIRE(f.service->status(session)->players.size() == 1);

    bool drained = false;
    f.service->drain(sessions::ShutdownCause::DrainUpdate, [&drained] { drained = true; });
    f.run_until_ended(session);
    CHECK(f.ended(session)->reason == sessions::StopReason::Update);
    f.strand.run_until([&] { return drained; });
    f.strand.advance(20s);
    CHECK(f.frames_of(*f.servers()[0], contract_frame_type_v<gs::Reset>) == 0);
    CHECK(f.servers().size() == 1);
}

TEST_CASE("a start's listing override survives an edit of other profile fields", "[host][service]") {
    Fixture f;
    f.connector.answer = false;
    const HostProfile profile = f.profile("Override");
    HostStartRequest request = f.request(profile);
    request.overrides.listing = HostListing::Unlisted;
    const SessionId session = f.started(f.start(std::move(request)));
    f.strand.advance(121s);
    REQUIRE(*f.service->phase(session) == HostPhase::LiveUnpublished);

    HostProfile edited = *f.profiles.get(profile.id);
    edited.description = "Fridays only";
    REQUIRE(f.service->update_profile(std::move(edited)));
    f.connector.answer = true;
    REQUIRE(f.strand.advance_until([&] { return *f.service->phase(session) == HostPhase::Live; }, 60s));
    CHECK(f.publisher.state(session)->hidden);
}

TEST_CASE("a Shutdown match-end policy stops the session as MatchEnded", "[host][service]") {
    Fixture f;
    f.script.events = {{1s, match_ended()}};
    const HostProfile profile =
        f.profile("Once", [](HostProfile& draft) { draft.match_end = MatchEndPolicy{MatchEndAction::Shutdown, 5s}; });
    const SessionId session = f.live(profile);
    f.strand.advance(7s);
    f.run_until_ended(session);
    CHECK(f.ended(session)->reason == sessions::StopReason::MatchEnded);
}

TEST_CASE("a server exiting on its own or reporting Fatal ends the session", "[host][service]") {
    Fixture f;
    const HostProfile crash = f.profile("Crash");
    const SessionId crashed = f.live(crash);
    f.servers()[0]->exit(ports::ChildExit{3, std::nullopt});
    f.run_until_ended(crashed);
    CHECK(f.ended(crashed)->reason == sessions::StopReason::Crashed);
    CHECK(f.ended(crashed)->exit_code == 3);
    CHECK(f.backend.released.empty());

    f.script.events = {{1s, gs::Fatal{"map_load", "missing pak"}}};
    const HostProfile fatal = f.profile("Fatal");
    const SessionId failing = f.live(fatal);
    f.strand.advance(2s);
    f.run_until_ended(failing);
    const std::optional<sessions::SessionEnded> ended = f.ended(failing);
    CHECK(ended->reason == sessions::StopReason::Fatal);
    REQUIRE(ended->error);
    CHECK(ended->error->id == "host.server_fatal");
}

TEST_CASE("operator commands pass through and ban edits are stored first", "[host][service]") {
    Fixture f;
    f.script.description.capabilities.operator_commands = {"start_match", "kick", "set_bans", "set_operators", "run_command"};
    const HostProfile profile = f.profile("Ops");
    const SessionId session = f.live(profile);

    std::optional<gameserver::CommandResult> said;
    REQUIRE(f.service->command(session, gameserver::RunCommand{"say hi"}, [&said](gameserver::CommandResult result) { said = result; }));
    f.strand.run_until([&] { return said.has_value(); });
    CHECK(said->status == gameserver::CommandStatus::Ok);
    CHECK(f.fakes[0]->commands() == std::vector<std::string>{"say hi"});
    CHECK(f.service->command(session, gameserver::EndMatch{}, [](gameserver::CommandResult) {}).error().id ==
          "gameserver.command_not_declared");
    CHECK(f.service->command(SessionId{test::profile_id(7).value}, gameserver::EndMatch{}, [](gameserver::CommandResult) {})
              .error()
              .id == "host.not_host_session");

    HostBan ban;
    ban.account_id = "Cheater-abc123";
    std::optional<gameserver::CommandResult> banned;
    REQUIRE(f.service->command(session, ReplaceBans{{ban}}, [&banned](gameserver::CommandResult result) { banned = result; }));
    f.strand.run_until([&] { return banned.has_value(); });
    CHECK(banned->status == gameserver::CommandStatus::Ok);
    REQUIRE(f.profiles.get(profile.id)->operators.bans.size() == 1);
    REQUIRE(f.fakes[0]->bans().size() == 1);
    CHECK(f.fakes[0]->bans()[0].address == "0.0.0.0/0");
    CHECK(f.fakes[0]->bans()[0].account_id == "Cheater-abc123");

    HostProfile edited = *f.profiles.get(profile.id);
    edited.operators.operator_cidrs = {*IpCidr::parse("192.168.1.0/24")};
    REQUIRE(f.service->update_profile(edited));
    f.strand.run_until([&] { return !f.fakes[0]->operator_cidrs().empty(); });
    CHECK(f.fakes[0]->operator_cidrs() == std::vector<std::string>{"192.168.1.0/24"});
    const std::vector<HostProfilesChanged> changes = f.events_of<HostProfilesChanged>(EventKind::HostProfilesChanged);
    CHECK(std::ranges::count_if(changes, [&](const HostProfilesChanged& change) {
              return change.change == HostProfileChange::Updated && change.profile == profile.id;
          }) >= 1);
}

TEST_CASE("a cancelled start stops the session it opened", "[host][service]") {
    Fixture f;
    f.script.listen_delay = 30s;
    const HostProfile profile = f.profile("Abort");
    const OpHandle op = f.start(f.request(profile));
    f.strand.run_until([&] { return f.servers().size() == 1 && !f.registry.list().empty(); });
    const SessionId session = f.registry.list().front().id;
    REQUIRE(f.ops.cancel(op.id(), CancelReason::User));
    CHECK(std::holds_alternative<Cancelled>(f.outcome(op)));
    f.strand.advance(10s);
    f.run_until_ended(session);
    CHECK(f.ended(session)->reason == sessions::StopReason::User);
    CHECK_FALSE(f.allocator.block(session));
}

TEST_CASE("a game server that needs the backend leases it for the session", "[host][service]") {
    Fixture f;
    f.script.description.capabilities.needs_backend = true;
    const HostProfile profile = f.profile("Backend");
    const SessionId session = f.started(f.start(f.request(profile)));
    REQUIRE(f.backend.accounts.size() == 1);
    const auto welcome = f.servers()[0]->stdin_frames().last<gs::ServerWelcome>();
    REQUIRE((*welcome)->config.backend.has_value());
    CHECK((*welcome)->config.backend->service_token == "service");
    REQUIRE(f.registry.stop(session, sessions::StopRequest{}, nullptr));
    f.run_until_ended(session);
    CHECK(f.backend.released == std::vector<SessionId>{session});

    f.backend.error = make_diag(ErrorDomain::Backend, MessageId{"backend.not_ready"}).build();
    const Diagnostic failed = f.failed(f.start(f.request(profile)));
    CHECK(failed.id == "backend.not_ready");
    f.strand.advance(10s);
    CHECK(f.registry.list().empty());
}

TEST_CASE("port mapping advertises the granted external port", "[host][service]") {
    Fixture f;
    const HostProfile profile = f.profile("Mapped", [](HostProfile& draft) { draft.port_mapping = true; });
    const SessionId session = f.started(f.start(f.request(profile)));
    f.run_until_phase(session, HostPhase::Live);
    const Port game = f.service->listening(session)->block.first;
    CHECK(f.publisher.state(session)->advertised_port == Port{static_cast<u16>(game.value + 1000)});
    CHECK(f.service->status(session)->mappings.size() == 1);
}

TEST_CASE("the linked auto-server follows its game and a second one runs unpublished", "[host][service]") {
    Fixture f;
    f.use_auto_profile_ports();
    const Result<SessionId> play = f.registry.open(sessions::SessionSpec{.kind = sessions::SessionKind::Play, .label = "play"},
                                                   std::make_unique<PlayDriver>());
    REQUIRE(play);
    HostStartRequest linked;
    linked.profile = kAutoProfileId;
    linked.linked_to = *play;
    linked.untested_confirmed = true;
    const SessionId first = f.started(f.start(linked));
    f.run_until_phase(first, HostPhase::Live);
    CHECK(f.publisher.state(first));
    CHECK(f.registry.get(first)->parent == *play);

    const SessionId second = f.started(f.start(linked));
    f.run_until_phase(second, HostPhase::LiveUnpublished);
    CHECK(f.service->status(second)->phase.reason->id == "host.second_auto_server_unpublished");

    REQUIRE(f.registry.stop(*play, sessions::StopRequest{}, nullptr));
    f.run_until_ended(*play);
    CHECK(f.ended(first)->reason == sessions::StopReason::ParentEnded);
    CHECK(f.ended(second)->reason == sessions::StopReason::ParentEnded);
}

TEST_CASE("an update drain follows each session's update policy", "[host][service]") {
    Fixture f;
    f.script.events = {{1s, gs::PlayerJoined{1, "Player-aaaaaa", "Player", "203.0.113.9"}}, {30s, match_ended()}};
    const HostProfile after_match = f.profile("After match");
    const HostProfile manual =
        f.profile("Manual", [](HostProfile& draft) {
            draft.update_policy = HostUpdatePolicy::Manual;
            draft.match_end.action = MatchEndAction::None;
        });
    const SessionId playing = f.live(after_match);
    const SessionId kept = f.live(manual);
    f.strand.advance(2s);
    REQUIRE(f.service->status(playing)->players.size() == 1);
    const std::vector<PlayerEvent> joined = f.events_of<PlayerEvent>(EventKind::PlayerEvent);
    REQUIRE_FALSE(joined.empty());
    CHECK(joined[0].player_count == 1);

    bool drained = false;
    f.service->drain(sessions::ShutdownCause::DrainUpdate, [&drained] { drained = true; });
    f.strand.run_ready();
    CHECK(*f.service->phase(playing) == HostPhase::Draining);
    CHECK(f.frames_of(*f.servers()[0], contract_frame_type_v<gs::Drain>) == 0);
    CHECK_FALSE(drained);

    f.strand.advance(30s);
    f.run_until_ended(playing);
    CHECK(f.ended(playing)->reason == sessions::StopReason::Update);
    f.strand.run_until([&] { return drained; });
    CHECK(*f.service->phase(kept) == HostPhase::Live);

    bool stopped = false;
    f.service->drain(sessions::ShutdownCause::Requested, [&stopped] { stopped = true; });
    f.strand.run_until([&] { return stopped; });
    CHECK(f.ended(kept)->reason == sessions::StopReason::EngineShutdown);
    CHECK(f.registry.list().empty());
}

TEST_CASE("profile writes publish HostProfilesChanged and a join password refresh reads the secret", "[host][service]") {
    Fixture f;
    const HostProfile profile = f.profile("Events");
    f.recorder.pump();
    REQUIRE(f.service->reset_profiles());
    f.recorder.pump();
    REQUIRE(f.service->delete_profile(profile.id));
    const std::vector<HostProfilesChanged> changes = f.events_of<HostProfilesChanged>(EventKind::HostProfilesChanged);
    REQUIRE(changes.size() == 3);
    CHECK(changes[0].change == HostProfileChange::Added);
    CHECK(changes[1].change == HostProfileChange::Reset);
    CHECK_FALSE(changes[1].profile.has_value());
    CHECK(changes[2].change == HostProfileChange::Removed);
    CHECK(f.service->delete_profile(kDefaultProfileId).error().id == "host.builtin_profile");
    CHECK(f.service->profiles().size() == 2);

    f.password_error = make_diag(ErrorDomain::Secrets, MessageId{"secrets.not_ready"}).build();
    CHECK(f.service->refresh_join_password(kDefaultProfileId).error().id == "secrets.not_ready");
    const HostProfile locked = f.profile("Locked");
    CHECK(f.failed(f.start(f.request(locked))).id == "secrets.not_ready");
    CHECK(f.registry.list().empty());
    f.password_error.reset();
    f.password = "new";
    CHECK(f.service->refresh_join_password(locked.id));
}
