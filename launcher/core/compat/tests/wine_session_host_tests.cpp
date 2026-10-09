#include <catch2/catch_test_macros.hpp>

#include <boost/asio/io_context.hpp>

#include <chrono>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <variant>
#include <vector>

#include "compat_test_support.hpp"
#include "reboot/compat/wine_session_host.hpp"
#include "reboot/contracts/winhost.hpp"
#include "reboot/foundation/executor.hpp"
#include "reboot/foundation/log.hpp"
#include "reboot/foundation/text.hpp"
#include "reboot/game_channel/game_channel_listener.hpp"
#include "reboot/game_channel/token_registry.hpp"
#include "reboot/process/env_builder.hpp"
#include "reboot/testing/fake_os.hpp"
#include "reboot/testing/fake_random.hpp"
#include "reboot/testing/fake_runner_platform.hpp"
#include "reboot/testing/fake_winhost.hpp"
#include "reboot/testing/game_control_bootstrap.hpp"
#include "reboot/testing/memory_stream_pair.hpp"
#include "reboot/testing/scripted_process_launcher.hpp"

using namespace reboot;
using namespace reboot::compat;
using namespace std::chrono_literals;

namespace {

namespace wh = contracts::winhost;

[[nodiscard]] std::string utf8_of(const NativePath& path) {
    const std::u8string text = path.u8string();
    return {text.begin(), text.end()};
}

[[nodiscard]] wh::Bytes utf16_bytes(std::string_view text) {
    wh::Bytes out;
    for (const char16_t unit : utf8_to_utf16(text)) {
        out.push_back(static_cast<u8>(unit & 0xFF));
        out.push_back(static_cast<u8>(unit >> 8));
    }
    return out;
}

[[nodiscard]] std::string value_of(const ports::EnvBlock& env, std::string_view name) {
    for (const auto& [key, value] : env.vars)
        if (key == name) return value;
    return {};
}

// Absolute on every host: "/" on POSIX, the drive root on Windows.
const NativePath kBase = std::filesystem::current_path().root_path();
const NativePath kBuild = kBase / "games" / "build";
const NativePath kExe = kBuild / "FortniteGame" / "Binaries" / "Win64" / "FortniteClient-Win64-Shipping.exe";
const NativePath kClientDll = kBase / "data" / "payload" / "rb_client.dll";
// Our DLL's token, which only the game's environment block carries.
constexpr std::string_view kGameToken = "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa";

struct Rig {
    Rig() {
        REQUIRE(listener.start());
        processes.on_exe("wine", [this](testing::ScriptedChild& child) -> Result<void> {
            runner_child = &child;
            if (!winhost_connects) return {};
            auto bootstrap = testing::FakeWinhost::bootstrap_of(child.launch());
            REQUIRE(bootstrap);
            winhost = std::make_unique<testing::FakeWinhost>(strand, clock, script);
            testing::MemoryStreamPair pair = testing::make_memory_stream_pair(strand, {}, {});
            listener.adopt(std::move(pair.a));
            winhost->attach(std::move(pair.b), *bootstrap);
            return {};
        });
    }

    ~Rig() {
        session.reset();
        winhost.reset();
        strand.run_all();
        listener.close();
    }

    [[nodiscard]] WineSessionSetup setup(RunnerKind kind = RunnerKind::Wine,
                                         std::vector<DosDevice> devices = {{'z', kBase}}) const {
        ports::RuntimeLayout layout;
        layout.root = kBase / "rt";
        layout.entry = kBase / "rt" / "bin" / "wine";
        process::EnvBuilder env(process::EnvSyntax::Posix);
        env.daemon_base(ports::EnvBlock{{{"HOME", "/home/player"}, {"LD_PRELOAD", "/evil.so"}}});
        return WineSessionSetup{kind,
                                layout,
                                kBase / "data" / "prefixes" / "wine",
                                PathMapper(std::move(devices)),
                                kBase / "data" / "payload" / "reboot-winhost.exe",
                                std::move(env),
                                kBase / "logs" / "wine"};
    }

    [[nodiscard]] ports::SessionLaunch launch() const {
        ports::SessionLaunch out;
        out.session = session_id;
        out.exe = kExe;
        out.args = {"-epicapp=Fortnite", "-AUTH_PASSWORD=hunter2"};
        out.env.vars = {{"REBOOT_CTL", "tcp://127.0.0.1:9"},
                        {"REBOOT_CTL_TOKEN", std::string(kGameToken)},
                        {"REBOOT_ROLE", "client"},
                        {"REBOOT_SESSION", format_uuid(session_id.value)}};
        out.cwd = kExe.parent_path();
        out.companions = {{kExe.parent_path() / "FortniteLauncher.exe", {}}};
        out.inject = {{kClientDll, {}, ports::BootStrategy::EarlyBirdApc, ports::InjectPhase::Early}};
        out.park = {kExe.parent_path() / "GFSDK_Aftermath_Lib.x64.dll"};
        out.multiplier = RunnerMultiplier::Wine;
        return out;
    }

    Result<void> start(RunnerKind kind = RunnerKind::Wine) {
        REQUIRE(host.stage(session_id, setup(kind)));
        auto launched = host.launch(launch(), [this](ports::SessionHostEvent event) { events.push_back(std::move(event)); });
        if (!launched) return std::unexpected(std::move(launched.error()));
        session = std::move(*launched);
        return {};
    }

    template <class E>
    [[nodiscard]] std::vector<E> of() const {
        std::vector<E> out;
        for (const ports::SessionHostEvent& event : events)
            if (const E* typed = std::get_if<E>(&event)) out.push_back(*typed);
        return out;
    }

    ManualClock clock;
    ManualExecutor strand{clock};
    TimerService timers{clock, strand};
    testing::FakeRandom random{7};
    Redactor redactor;
    game_channel::TokenRegistry tokens{random, redactor};
    boost::asio::io_context io;
    game_channel::GameChannelListener listener{io, strand, timers, tokens};
    testing::ScriptedProcessLauncher processes{strand, clock, testing::FakeOs::Linux};
    testing::FakeRunnerPlatform runner{{RunnerKind::Umu, RunnerKind::Wine}};
    std::vector<std::string> wine_lines;
    WineSessionHost host{WineSessionHostDeps{processes, runner, listener, timers, strand,
                                             [this](SessionId, std::string_view line) { wine_lines.emplace_back(line); }}};

    SessionId session_id{Uuid{{0x42}}};
    testing::FakeWinhostScript script;
    bool winhost_connects = true;
    testing::ScriptedChild* runner_child = nullptr;
    std::unique_ptr<testing::FakeWinhost> winhost;
    std::vector<ports::SessionHostEvent> events;
    std::unique_ptr<ports::IGameSession> session;
};

}  // namespace

TEST_CASE("a session is staged once and launched once", "[compat][wine_session_host]") {
    Rig rig;
    REQUIRE(rig.host.stage(rig.session_id, rig.setup()));
    const auto twice = rig.host.stage(rig.session_id, rig.setup());
    REQUIRE_FALSE(twice);
    CHECK(twice.error().id == "compat.session_already_staged");

    rig.host.discard(rig.session_id);
    const auto launched = rig.host.launch(rig.launch(), [](ports::SessionHostEvent) {});
    REQUIRE_FALSE(launched);
    CHECK(launched.error().id == "compat.session_not_staged");
    CHECK(rig.processes.children().empty());
}

TEST_CASE("winhost runs under the runner with only its own channel layer", "[compat][wine_session_host]") {
    Rig rig;
    REQUIRE(rig.start());
    CHECK(rig.events.empty());
    REQUIRE(rig.runner_child != nullptr);
    const ports::ProcessLaunch& launch = rig.runner_child->launch();
    CHECK(launch.args == std::vector<std::string>{utf8_of(kBase / "data" / "payload" / "reboot-winhost.exe")});
    CHECK(launch.scope_name == "reboot-session-" + format_uuid(rig.session_id.value));
    CHECK(value_of(launch.env, "HOME") == "/home/player");
    CHECK(value_of(launch.env, "LD_PRELOAD").empty());
    CHECK(value_of(launch.env, "WINEDLLOVERRIDES") == "winemenubuilder.exe=d");
    CHECK(value_of(launch.env, "REBOOT_ROLE") == "winhost");
    CHECK(value_of(launch.env, "REBOOT_SESSION") == format_uuid(rig.session_id.value));
    CHECK(value_of(launch.env, "REBOOT_CTL").starts_with("tcp://127.0.0.1:"));
    CHECK(value_of(launch.env, "REBOOT_CTL_TOKEN") != kGameToken);
    CHECK(value_of(launch.env, "WINEPREFIX") == utf8_of(kBase / "data" / "prefixes" / "wine"));
}

TEST_CASE("winhost gets the game in Windows paths and relays its events", "[compat][wine_session_host]") {
    Rig rig;
    REQUIRE(rig.start());
    // Before the Welcome, so it waits for it.
    REQUIRE(rig.session->resume());
    rig.strand.run_all();

    REQUIRE(rig.winhost);
    const auto spawn = rig.winhost->spawn_request();
    REQUIRE(spawn);
    CHECK(spawn->exe_utf16 == utf16_bytes("Z:\\games\\build\\FortniteGame\\Binaries\\Win64\\FortniteClient-Win64-Shipping.exe"));
    CHECK(spawn->cwd_utf16 == utf16_bytes("Z:\\games\\build\\FortniteGame\\Binaries\\Win64"));
    CHECK(spawn->argv_utf16 == std::vector<wh::Bytes>{utf16_bytes("-epicapp=Fortnite"), utf16_bytes("-AUTH_PASSWORD=hunter2")});
    REQUIRE(spawn->companions.size() == 1);
    CHECK(spawn->companions[0].exe_utf16 == utf16_bytes("Z:\\games\\build\\FortniteGame\\Binaries\\Win64\\FortniteLauncher.exe"));
    REQUIRE(spawn->inject.size() == 1);
    CHECK(spawn->inject[0].path_utf16 == utf16_bytes("Z:\\data\\payload\\rb_client.dll"));
    CHECK(spawn->park_utf16 ==
          std::vector<wh::Bytes>{utf16_bytes("Z:\\games\\build\\FortniteGame\\Binaries\\Win64\\GFSDK_Aftermath_Lib.x64.dll")});
    CHECK(spawn->inject_timeout_ms == 20'000);
    CHECK(spawn->drain_timeout_ms == 10'000);
    const auto game_bootstrap = testing::read_game_control_bootstrap(spawn->env_block_utf16);
    REQUIRE(game_bootstrap);
    CHECK(game_bootstrap->role == "client");
    CHECK(game_bootstrap->session == rig.session_id);
    CHECK(rig.winhost->resumed());

    const auto spawned = rig.of<ports::Spawned>();
    REQUIRE(spawned.size() == 2);
    CHECK(spawned[0].role == ports::SessionRole::Game);
    CHECK(spawned[1].role == ports::SessionRole::Companion);
    const auto injected = rig.of<ports::Injected>();
    REQUIRE(injected.size() == 1);
    CHECK(injected[0].path == kClientDll);
    CHECK(injected[0].ok);
}

TEST_CASE("a late injection that fails comes back with its Windows error", "[compat][wine_session_host]") {
    Rig rig;
    rig.script.failing_injections = {"auth.dll"};
    REQUIRE(rig.start());
    rig.strand.run_all();
    const NativePath auth = kBase / "custom" / "auth.dll";
    REQUIRE(rig.session->inject({auth, {}, ports::BootStrategy::AfterResume, ports::InjectPhase::LoggedIn}));
    rig.strand.run_all();
    const auto injected = rig.of<ports::Injected>();
    REQUIRE(injected.size() == 2);
    CHECK(injected[1].path == auth);
    CHECK_FALSE(injected[1].ok);
    CHECK(injected[1].error == SystemError{SystemError::Origin::GuestWindows, testing::kFakeInjectionError});
}

TEST_CASE("a stopped session ends with the game's exit and no fatal", "[compat][wine_session_host]") {
    Rig rig;
    REQUIRE(rig.start());
    rig.strand.run_all();
    rig.session->stop(1500ms);
    rig.strand.run_all();
    CHECK(rig.winhost->stop_grace_ms() == 1500u);
    CHECK(rig.of<ports::HostFatal>().empty());
    const auto exited = rig.of<ports::Exited>();
    REQUIRE(exited.size() == 2);
    CHECK(exited[0].role == ports::SessionRole::Game);
    CHECK(exited[1].role == ports::SessionRole::Winhost);

    // Past the grace the runner tree goes too.
    CHECK_FALSE(rig.runner_child->terminated());
    rig.strand.advance(1500ms);
    CHECK(rig.runner_child->terminated());
    CHECK(rig.of<ports::Exited>().size() == 2);
}

TEST_CASE("winhost going away mid-game is fatal", "[compat][wine_session_host]") {
    Rig rig;
    REQUIRE(rig.start());
    rig.strand.run_all();
    rig.winhost->disconnect();
    rig.strand.run_all();
    const auto fatal = rig.of<ports::HostFatal>();
    REQUIRE(fatal.size() == 1);
    CHECK(fatal[0].error.id == "game_channel.peer_lost");
    REQUIRE(std::holds_alternative<ports::Exited>(rig.events.back()));
    CHECK(std::get<ports::Exited>(rig.events.back()).role == ports::SessionRole::Winhost);
    CHECK_FALSE(rig.session->resume());
}

TEST_CASE("the game exiting first makes winhost's end ordinary", "[compat][wine_session_host]") {
    Rig rig;
    REQUIRE(rig.start());
    rig.strand.run_all();
    rig.winhost->game_exits(0);
    rig.winhost->disconnect();
    rig.strand.run_all();
    CHECK(rig.of<ports::HostFatal>().empty());
    CHECK(rig.of<ports::Exited>().size() == 2);
}

TEST_CASE("a winhost failure is a host fatal with the code from inside the prefix", "[compat][wine_session_host]") {
    Rig rig;
    REQUIRE(rig.start());
    rig.strand.run_all();
    rig.winhost->send(wh::WhFatal{"spawn", 193});
    rig.strand.run_all();
    const auto fatal = rig.of<ports::HostFatal>();
    REQUIRE(fatal.size() == 1);
    CHECK(fatal[0].error.id == "compat.winhost_fatal");
    CHECK(fatal[0].error.os_error == SystemError{SystemError::Origin::GuestWindows, 193});
}

TEST_CASE("the runner exiting before winhost connects ends the session", "[compat][wine_session_host]") {
    Rig rig;
    rig.winhost_connects = false;
    REQUIRE(rig.start());
    rig.runner_child->write_stderr_line("wine: could not load kernel32.dll");
    rig.runner_child->exit({2, std::nullopt});
    rig.strand.run_all();
    CHECK(rig.wine_lines == std::vector<std::string>{"wine: could not load kernel32.dll"});
    const auto fatal = rig.of<ports::HostFatal>();
    REQUIRE(fatal.size() == 1);
    CHECK(fatal[0].error.id == "compat.runner_exited");
    const auto exited = rig.of<ports::Exited>();
    REQUIRE(exited.size() == 1);
    CHECK(exited[0].role == ports::SessionRole::Winhost);
    CHECK(exited[0].code == 2);
}

TEST_CASE("a stop before the Welcome kills the runner at once", "[compat][wine_session_host]") {
    Rig rig;
    rig.winhost_connects = false;
    REQUIRE(rig.start());
    rig.session->stop(5s);
    rig.strand.run_all();
    CHECK(rig.runner_child->terminated());
    CHECK(rig.of<ports::HostFatal>().empty());
    CHECK(rig.of<ports::Exited>().size() == 1);
}

TEST_CASE("a game outside every drive is refused before anything runs", "[compat][wine_session_host]") {
    Rig rig;
    REQUIRE(rig.host.stage(rig.session_id, rig.setup(RunnerKind::Wine, {{'c', kBase / "elsewhere"}})));
    const auto launched = rig.host.launch(rig.launch(), [](ports::SessionHostEvent) {});
    REQUIRE_FALSE(launched);
    CHECK(launched.error().id == "compat.path_not_mapped");
    CHECK(rig.processes.children().empty());
    CHECK_FALSE(rig.host.launch(rig.launch(), [](ports::SessionHostEvent) {}));
}

TEST_CASE("a runner that cannot start fails the launch", "[compat][wine_session_host]") {
    Rig rig;
    rig.runner.faults().fail_next(testing::RunnerOperation::RunnerLaunch, internal_bug("test"));
    const auto started = rig.start();
    REQUIRE_FALSE(started);
    CHECK(started.error().id == "compat.runner_spawn_failed");
    REQUIRE(started.error().causes.size() == 1);
    CHECK(started.error().causes[0].id == "internal.bug");
}

TEST_CASE("under umu pressure-vessel shares the build, the DLLs and the log folder", "[compat][wine_session_host]") {
    Rig rig;
    const auto started = rig.start(RunnerKind::Umu);
    // A Windows host's drive letters carry the colon pressure-vessel splits on.
    if (utf8_of(kBase).find(':') != std::string::npos) {
        REQUIRE_FALSE(started);
        CHECK(started.error().id == "compat.path_not_exposable");
        return;
    }
    REQUIRE(started);
    CHECK(value_of(rig.runner_child->launch().env, "PRESSURE_VESSEL_FILESYSTEMS_RW") ==
          utf8_of(kBuild) + ":" + utf8_of(kClientDll.parent_path()) + ":" + utf8_of(kBase / "logs" / "wine"));
}
