// CI gate (spec 14.6): the real reboot-winhost.exe and reboot-fake-game.exe under the pinned Kron4ek
// Wine, launched through LinuxRunnerPlatform and PidfdProcessLauncher and driven over loopback as the
// engine drives them. The wine job (ci/wine/run.sh) sets REBOOT_WINE_RUNTIME, REBOOT_WINHOST_EXE and
// REBOOT_FAKE_GAME_EXE; the umu job (ci/umu/run.sh) sets REBOOT_UMU_LAUNCHER and REBOOT_GE_PROTON
// instead of REBOOT_WINE_RUNTIME for the [umu] case. Everywhere else every case skips.
#include <arpa/inet.h>
#include <netinet/in.h>
#include <poll.h>
#include <signal.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <unistd.h>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <initializer_list>
#include <iterator>
#include <memory>
#include <mutex>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

#include "reboot/contracts/common.hpp"
#include "reboot/contracts/game_client.hpp"
#include "reboot/contracts/winhost.hpp"
#include "reboot/foundation/cancel.hpp"
#include "reboot/foundation/framing.hpp"
#include "reboot/foundation/log.hpp"
#include "reboot/foundation/random.hpp"
#include "reboot/foundation/text.hpp"
#include "reboot/foundation/version.hpp"
#include "reboot/os_linux/platform/pidfd_process_launcher.hpp"
#include "reboot/os_linux/runner/linux_runner_platform.hpp"
#include "reboot/os_linux/runner/umu_invocation.hpp"
#include "reboot/testing/scratch_dir.hpp"

namespace fs = std::filesystem;
using namespace rb;
using namespace std::chrono_literals;
namespace wh = rb::contracts::winhost;
namespace gc = rb::contracts::game_client;
namespace common = rb::contracts::common;
using os_linux::platform::PidfdProcessLauncher;
using os_linux::runner::LinuxRunnerPlatform;
using os_linux::runner::UmuInvocation;

namespace {

// Wine's first start in a fresh prefix brings its services up, and CI runners are slow.
constexpr auto kWineStart = 120s;
constexpr auto kPrefixBoot = 300s;
constexpr auto kExit = 30s;

// WinhostExit, which the wine loader returns as its own exit status.
constexpr int kEngineClosed = 0;
constexpr int kStopped = 1;
constexpr int kLaunchFailed = 5;

constexpr std::string_view kWinhostName = "reboot-winhost.exe";
constexpr std::string_view kGameName = "reboot-fake-game.exe";
constexpr std::string_view kPassword = "wine-gate-password-5f1c";
constexpr std::string_view kSession = "6f1e2d3c-4b5a-4978-8a9b-0c1d2e3f4a5b";

[[nodiscard]] std::optional<std::string> env_value(const char* name) {
    const char* value = std::getenv(name);
    if (value == nullptr || *value == '\0') return std::nullopt;
    return std::string(value);
}

struct Inputs {
    NativePath runtime;
    NativePath winhost;
    NativePath game;
};

[[nodiscard]] std::optional<Inputs> inputs() {
    auto runtime = env_value("REBOOT_WINE_RUNTIME");
    auto winhost = env_value("REBOOT_WINHOST_EXE");
    auto game = env_value("REBOOT_FAKE_GAME_EXE");
    if (!runtime || !winhost || !game) return std::nullopt;
    return Inputs{NativePath(*runtime), NativePath(*winhost), NativePath(*game)};
}

constexpr std::string_view kNoWine = "REBOOT_WINE_RUNTIME, REBOOT_WINHOST_EXE and REBOOT_FAKE_GAME_EXE name no Wine to test under";

// umu-launcher's extracted zipapp and an extracted GE-Proton, as the component store holds them.
struct UmuInputs {
    NativePath launcher;
    NativePath proton;
    NativePath winhost;
    NativePath game;
};

[[nodiscard]] std::optional<UmuInputs> umu_inputs() {
    auto launcher = env_value("REBOOT_UMU_LAUNCHER");
    auto proton = env_value("REBOOT_GE_PROTON");
    auto winhost = env_value("REBOOT_WINHOST_EXE");
    auto game = env_value("REBOOT_FAKE_GAME_EXE");
    if (!launcher || !proton || !winhost || !game) return std::nullopt;
    return UmuInputs{NativePath(*launcher), NativePath(*proton), NativePath(*winhost), NativePath(*game)};
}

constexpr std::string_view kNoUmu =
    "REBOOT_UMU_LAUNCHER, REBOOT_GE_PROTON, REBOOT_WINHOST_EXE and REBOOT_FAKE_GAME_EXE name no umu to test under";

// umu downloads the Steam Linux Runtime, and Proton's first prefix setup is slower than wineboot.
constexpr auto kUmuSetup = 900s;

[[nodiscard]] ports::EnvBlock base_env() {
    ports::EnvBlock env;
    env.vars.emplace_back("PATH", env_value("PATH").value_or("/usr/bin:/bin"));
    for (const char* name : {"HOME", "USER", "LANG"})
        if (auto value = env_value(name)) env.vars.emplace_back(name, std::move(*value));
    env.vars.emplace_back("WINEDEBUG", "-all");
    // As compat's runner layer, plus no Mono or Gecko prompt in a fresh prefix.
    env.vars.emplace_back("WINEDLLOVERRIDES", "winemenubuilder.exe=d;mscoree=d;mshtml=d");
    return env;
}

[[nodiscard]] std::string base64url(std::span<const u8> bytes) {
    constexpr std::string_view kAlphabet = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";
    std::string out;
    u32 bits = 0;
    int pending = 0;
    for (const u8 byte : bytes) {
        bits = (bits << 8) | byte;
        pending += 8;
        while (pending >= 6) {
            pending -= 6;
            out += kAlphabet[(bits >> pending) & 0x3F];
        }
    }
    if (pending > 0) out += kAlphabet[(bits << (6 - pending)) & 0x3F];
    return out;
}

[[nodiscard]] std::array<u8, 32> fresh_token() {
    OsRandom random;
    return random_bytes<32>(random);
}

[[nodiscard]] std::vector<u8> utf16(std::string_view text) {
    std::vector<u8> out;
    for (const char16_t unit : utf8_to_utf16(text)) {
        out.push_back(static_cast<u8>(unit & 0xFF));
        out.push_back(static_cast<u8>(unit >> 8));
    }
    return out;
}

// Wine's Z: drive is the host root.
[[nodiscard]] std::string windows_path(const NativePath& path) {
    std::string out = "Z:" + fs::absolute(path).lexically_normal().string();
    for (char& c : out)
        if (c == '/') c = '\\';
    return out;
}

[[nodiscard]] std::string read_all(const NativePath& file) {
    std::ifstream in(file, std::ios::binary);
    return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

// The live (not zombie) host processes of our prefix whose command line names `exe`.
[[nodiscard]] std::vector<u32> wine_processes(const NativePath& prefix, std::string_view exe) {
    const std::string marker = "WINEPREFIX=" + prefix.string();
    std::vector<u32> out;
    std::error_code error;
    for (const auto& entry : fs::directory_iterator("/proc", error)) {
        const std::string name = entry.path().filename().string();
        if (name.empty() || name.find_first_not_of("0123456789") != std::string::npos) continue;
        if (read_all(entry.path() / "cmdline").find(exe) == std::string::npos) continue;
        const std::string environ = read_all(entry.path() / "environ");
        if (environ.find(marker + '\0') == std::string::npos && !environ.ends_with(marker)) continue;
        const std::string stat = read_all(entry.path() / "stat");
        const std::size_t state = stat.rfind(") ");
        if (state != std::string::npos && state + 2 < stat.size() && stat[state + 2] == 'Z') continue;
        out.push_back(static_cast<u32>(std::stoul(name)));
    }
    return out;
}

[[nodiscard]] std::optional<u32> wait_for_process(const NativePath& prefix, std::string_view exe, std::chrono::seconds limit) {
    const auto give_up = std::chrono::steady_clock::now() + limit;
    for (;;) {
        if (const auto found = wine_processes(prefix, exe); !found.empty()) return found.front();
        if (std::chrono::steady_clock::now() > give_up) return std::nullopt;
        std::this_thread::sleep_for(100ms);
    }
}

[[nodiscard]] bool wait_until_gone(const NativePath& prefix, std::string_view exe, std::chrono::seconds limit) {
    const auto give_up = std::chrono::steady_clock::now() + limit;
    while (!wine_processes(prefix, exe).empty()) {
        if (std::chrono::steady_clock::now() > give_up) return false;
        std::this_thread::sleep_for(100ms);
    }
    return true;
}

// A captured child: its output kept for the failure message, its exit waited for.
class Child {
public:
    explicit Child(std::unique_ptr<ports::ChildProcess> process) : process_(std::move(process)) {
        process_->on_stdout([this](std::span<const u8> bytes) { keep(bytes); });
        process_->on_stderr([this](std::span<const u8> bytes) { keep(bytes); });
        process_->on_exit([this](ports::ChildExit exit) {
            const std::scoped_lock lock(mutex_);
            exit_ = exit;
            changed_.notify_all();
        });
    }

    // The handle goes first: it waits out a callback in flight and drops the rest.
    ~Child() {
        if (!exit_within(0s)) (void)process_->terminate_tree();
        process_.reset();
    }

    Child(const Child&) = delete;
    Child& operator=(const Child&) = delete;

    [[nodiscard]] u32 pid() const { return process_->pid(); }

    void write_stdin(std::string_view text) {
        process_->write_stdin(std::span(reinterpret_cast<const u8*>(text.data()), text.size()));
    }

    [[nodiscard]] std::optional<ports::ChildExit> exit_within(std::chrono::seconds limit) {
        std::unique_lock lock(mutex_);
        changed_.wait_for(lock, limit, [this] { return exit_.has_value(); });
        return exit_;
    }

    [[nodiscard]] std::string output() {
        const std::scoped_lock lock(mutex_);
        return output_;
    }

private:
    void keep(std::span<const u8> bytes) {
        const std::scoped_lock lock(mutex_);
        output_.append(bytes.begin(), bytes.end());
    }

    std::unique_ptr<ports::ChildProcess> process_;
    std::mutex mutex_;
    std::condition_variable changed_;
    std::optional<ports::ChildExit> exit_;
    std::string output_;
};

// One loopback listener and the connection it accepts. Reads time out, so a frame that never comes
// fails the case instead of hanging it.
class Loopback {
public:
    struct Frame {
        u64 type = 0;
        std::vector<u8> payload;
    };

    Loopback() {
        listener_ = ::socket(AF_INET, SOCK_STREAM | SOCK_CLOEXEC, 0);
        sockaddr_in address{};
        address.sin_family = AF_INET;
        address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        socklen_t size = sizeof(address);
        if (listener_ < 0 || ::bind(listener_, reinterpret_cast<const sockaddr*>(&address), sizeof(address)) != 0 ||
            ::listen(listener_, 4) != 0 || ::getsockname(listener_, reinterpret_cast<sockaddr*>(&address), &size) != 0)
            return;
        port_ = ntohs(address.sin_port);
    }

    ~Loopback() {
        close_peer();
        if (listener_ >= 0) ::close(listener_);
    }

    Loopback(const Loopback&) = delete;
    Loopback& operator=(const Loopback&) = delete;

    [[nodiscard]] u16 port() const noexcept { return port_; }
    [[nodiscard]] std::string url() const { return "tcp://127.0.0.1:" + std::to_string(port_); }

    [[nodiscard]] bool accept_peer(std::chrono::seconds limit) {
        if (!readable(listener_, limit)) return false;
        peer_ = ::accept4(listener_, nullptr, nullptr, SOCK_CLOEXEC);
        return peer_ >= 0;
    }

    void close_peer() {
        if (peer_ >= 0) ::close(peer_);
        peer_ = -1;
    }

    [[nodiscard]] bool preamble_ok() {
        std::array<u8, kGameControlPreambleSize> bytes{};
        return read_exact(bytes.data(), bytes.size(), kWineStart) && bytes == game_control_preamble(VersionStreams::payload_abi);
    }

    // nullopt on EOF, an error or a timeout.
    [[nodiscard]] std::optional<Frame> read_frame(std::chrono::seconds limit = kExit) {
        Frame frame;
        std::array<u64, 2> header{};
        for (u64& value : header) {
            std::array<u8, 8> raw{};
            if (!read_exact(raw.data(), 1, limit)) return std::nullopt;
            const std::size_t width = std::size_t{1} << (raw[0] >> 6);
            if (!read_exact(raw.data() + 1, width - 1, limit)) return std::nullopt;
            value = raw[0] & 0x3Fu;
            for (std::size_t i = 1; i < width; ++i) value = (value << 8) | raw[i];
        }
        frame.type = header[0];
        frame.payload.resize(static_cast<std::size_t>(header[1]));
        if (!read_exact(frame.payload.data(), frame.payload.size(), limit)) return std::nullopt;
        return frame;
    }

    // The next frame of type T; others are kept in `seen`.
    template <ContractMessage T>
    [[nodiscard]] std::optional<T> expect(std::chrono::seconds limit = kExit) {
        while (auto frame = read_frame(limit)) {
            if (frame->type != contract_frame_type_v<T>) {
                seen.push_back(std::move(*frame));
                continue;
            }
            auto message = decode_contract<T>(std::span<const u8>(frame->payload));
            if (!message) return std::nullopt;
            return std::move(*message);
        }
        return std::nullopt;
    }

    // True once the peer has closed its end; every frame before that is kept in `seen`.
    [[nodiscard]] bool drain_to_eof(std::chrono::seconds limit = kExit) {
        while (auto frame = read_frame(limit)) seen.push_back(std::move(*frame));
        return eof_;
    }

    template <ContractMessage T>
    void send(const T& message) {
        const std::vector<u8> bytes = encode_contract_frame(message);
        std::size_t sent = 0;
        while (sent < bytes.size()) {
            const ssize_t n = ::send(peer_, bytes.data() + sent, bytes.size() - sent, MSG_NOSIGNAL);
            if (n <= 0) return;
            sent += static_cast<std::size_t>(n);
        }
    }

    template <ContractMessage T>
    [[nodiscard]] std::vector<T> seen_of() const {
        std::vector<T> out;
        for (const Frame& frame : seen)
            if (frame.type == contract_frame_type_v<T>)
                if (auto message = decode_contract<T>(std::span<const u8>(frame.payload))) out.push_back(std::move(*message));
        return out;
    }

    std::vector<Frame> seen;

private:
    [[nodiscard]] static bool readable(int fd, std::chrono::seconds limit) {
        pollfd entry{fd, POLLIN, 0};
        return ::poll(&entry, 1, static_cast<int>(std::chrono::milliseconds(limit).count())) == 1;
    }

    [[nodiscard]] bool read_exact(u8* out, std::size_t size, std::chrono::seconds limit) {
        std::size_t got = 0;
        while (got < size) {
            if (peer_ < 0 || !readable(peer_, limit)) return false;
            const ssize_t n = ::recv(peer_, out + got, size - got, 0);
            if (n <= 0) {
                eof_ = n == 0;
                return false;
            }
            got += static_cast<std::size_t>(n);
        }
        return true;
    }

    int listener_ = -1;
    int peer_ = -1;
    u16 port_ = 0;
    bool eof_ = false;
};

// As the engine picks it: under $XDG_RUNTIME_DIR when that is usable, else under `data_root`.
[[nodiscard]] NativePath token_base(const NativePath& data_root) {
    const std::optional<std::string> runtime = env_value("XDG_RUNTIME_DIR");
    return LinuxRunnerPlatform::token_base(runtime ? std::optional<std::string_view>{*runtime} : std::nullopt, data_root);
}

// The prefix every case shares, booted once: REBOOT_WINE_PREFIX, or a scratch directory.
const NativePath& prefix(const Inputs& in) {
    static const NativePath path = [&] {
        NativePath dir;
        if (auto named = env_value("REBOOT_WINE_PREFIX")) {
            dir = NativePath(*named);
        } else {
            OsRandom random;
            static auto scratch = testing::ScratchDir::create(random, "reboot-wine-gate");
            REQUIRE(scratch);
            dir = scratch->path() / "prefix";
        }
        if (fs::exists(dir / "system.reg")) return dir;
        PidfdProcessLauncher launcher{std::nullopt};
        LinuxRunnerPlatform runner{launcher, base_env(), dir.parent_path() / "umu", token_base(dir.parent_path())};
        const auto layout = runner.layout(ports::RunnerKind::Wine, {in.runtime, std::nullopt});
        REQUIRE(layout);
        const auto boot = runner.prefix_command(*layout, dir, {ports::PrefixVerb::Boot, {}, {}}, base_env());
        REQUIRE(boot);
        auto spawned = launcher.spawn(*boot);
        REQUIRE(spawned);
        Child wineboot(std::move(*spawned));
        const auto exit = wineboot.exit_within(kPrefixBoot);
        INFO(wineboot.output());
        REQUIRE(exit);
        REQUIRE(exit->code == 0);
        return dir;
    }();
    return path;
}

// reboot-winhost.exe started as WineSessionHost starts it: runner_launch with REBOOT_CTL, and its
// token file's Windows path as its last argument, the file's directory shared read-only under umu.
struct Winhost {
    std::array<u8, 32> token = fresh_token();
    PidfdProcessLauncher launcher{std::nullopt};
    std::unique_ptr<ports::ITokenFile> token_file;
    NativePath token_path;
    std::optional<Child> child;

    // The failure, or empty once winhost runs.
    [[nodiscard]] std::string start(const Inputs& in, const NativePath& prefix_dir, const Loopback& engine) {
        LinuxRunnerPlatform runner{launcher, base_env(), prefix_dir.parent_path() / "umu",
                                   token_base(prefix_dir.parent_path())};
        const auto layout = runner.layout(ports::RunnerKind::Wine, {in.runtime, std::nullopt});
        if (!layout) return "layout: " + layout.error().id;
        return start_under(runner, *layout, prefix_dir, in.winhost, engine, base_env());
    }

    // `env` holds the runner layer, as EnvBuilder's runner layer does.
    [[nodiscard]] std::string start_under(LinuxRunnerPlatform& runner, const ports::RuntimeLayout& layout,
                                          const NativePath& prefix_dir, const NativePath& winhost,
                                          const Loopback& engine, ports::EnvBlock env) {
        auto file = runner.write_token_file(base64url(token));
        if (!file) return "write_token_file: " + file.error().id;
        token_file = std::move(*file);
        token_path = token_file->path();
        env.vars.emplace_back(gc::kEnvCtl, engine.url());
        if (UmuInvocation::is_umu_layout(layout))
            env.vars.emplace_back("PRESSURE_VESSEL_FILESYSTEMS_RO", token_path.parent_path().string());
        auto launch = runner.runner_launch(layout, prefix_dir, winhost, std::move(env));
        if (!launch) return "runner_launch: " + launch.error().id;
        launch->args.push_back(windows_path(token_path));
        auto spawned = launcher.spawn(*launch);
        if (!spawned) return "spawn: " + spawned.error().id;
        child.emplace(std::move(*spawned));
        return {};
    }

    // What WineSessionHost does on winhost's Hello.
    void connected() { token_file.reset(); }
};

// The runtime setup's umu output reaches only the log, so the job's console carries it.
class StderrSink final : public LogSink {
public:
    void write(std::span<const LogRecord> records) override {
        for (const LogRecord& record : records) {
            const std::string_view category = log_category_name(record.category);
            std::fprintf(stderr, "[%.*s] %s\n", static_cast<int>(category.size()), category.data(), record.text.c_str());
        }
    }
    void flush() override { std::fflush(stderr); }
};

// The daemon base and umu's runner layer; pressure-vessel also shares the game's directory, as
// WineSessionHost shares the build's.
[[nodiscard]] ports::EnvBlock umu_env(const ports::RuntimeLayout& layout, const NativePath& game) {
    ports::EnvBlock env = base_env();
    for (const auto& variable : layout.env) env.vars.push_back(variable);
    env.vars.emplace_back("PRESSURE_VESSEL_FILESYSTEMS_RW", game.parent_path().string());
    return env;
}

// The game's bootstrap, as play's channel layer sets it.
[[nodiscard]] std::vector<u8> game_env_block(const Loopback& game_channel, const std::array<u8, 32>& token) {
    std::string block;
    for (const auto& [name, value] : std::initializer_list<std::pair<std::string_view, std::string>>{
             {gc::kEnvCtl, game_channel.url()},
             {gc::kEnvCtlToken, base64url(token)},
             {gc::kEnvSession, std::string(kSession)},
             {gc::kEnvRole, "client"}}) {
        block.append(name);
        block += '=';
        block += value;
        block += '\0';
    }
    block += '\0';
    return utf16(block);
}

[[nodiscard]] wh::SpawnGame spawn_game(const NativePath& exe, std::vector<u8> env_block) {
    wh::SpawnGame spawn;
    spawn.exe_utf16 = utf16(windows_path(exe));
    for (const std::string& arg : {"-AUTH_PASSWORD=" + std::string(kPassword), std::string("-epicapp=Fortnite")})
        spawn.argv_utf16.push_back(utf16(arg));
    spawn.env_block_utf16 = std::move(env_block);
    spawn.cwd_utf16 = utf16(windows_path(exe.parent_path()));
    spawn.inject_timeout_ms = 30000;
    spawn.drain_timeout_ms = 30000;
    return spawn;
}

// Hello read, the spawn sent, Spawned{Game} and an ok Resume back; the failure, or empty.
[[nodiscard]] std::string launch_game(Loopback& engine, Winhost& winhost, wh::SpawnGame spawn) {
    if (!engine.accept_peer(kWineStart)) return "winhost never connected";
    if (!engine.preamble_ok()) return "no game-control preamble";
    const auto hello = engine.expect<wh::WhHello>();
    if (!hello) return "no WhHello";
    if (hello->token != winhost.token) return "WhHello carries another token";
    winhost.connected();
    engine.send(wh::WhWelcome{std::move(spawn)});
    const auto spawned = engine.expect<wh::Spawned>(kWineStart);
    if (!spawned || spawned->role != wh::ProcessRole::Game) return "no Spawned{Game}";
    engine.send(wh::Resume{1});
    const auto resumed = engine.expect<common::CommandResult>();
    if (!resumed || resumed->req_id != 1 || !resumed->ok) return "Resume failed";
    return {};
}

// The game's own channel connection: its GcHello carries the token only the env overlay held.
void expect_game_hello(Loopback& game_channel, const std::array<u8, 32>& token) {
    REQUIRE(game_channel.accept_peer(kWineStart));
    REQUIRE(game_channel.preamble_ok());
    const auto hello = game_channel.expect<gc::GcHello>(kWineStart);
    REQUIRE(hello);
    CHECK(hello->token == token);
}

}  // namespace

TEST_CASE("winhost under Wine starts the game with the env overlay, relays it, and a Stop ends both", "[wine]") {
    const std::optional<Inputs> in = inputs();
    if (!in) SKIP(kNoWine);
    const NativePath& dir = prefix(*in);
    Loopback engine;
    Loopback game_channel;
    Winhost winhost;
    REQUIRE(engine.port() != 0);
    REQUIRE(game_channel.port() != 0);
    const std::array<u8, 32> game_token = fresh_token();

    REQUIRE(winhost.start(*in, dir, engine) == "");
    const std::string launched = launch_game(engine, winhost, spawn_game(in->game, game_env_block(game_channel, game_token)));
    INFO(winhost.child->output());
    REQUIRE(launched == "");
    expect_game_hello(game_channel, game_token);

    // Stop is answered from the request thread; winhost's exit shows its shutdown woke the blocked recv.
    engine.send(wh::Stop{2, 500});
    const auto stopped = engine.expect<common::CommandResult>();
    REQUIRE(stopped);
    CHECK(stopped->req_id == 2);
    CHECK(stopped->ok);
    CHECK(engine.drain_to_eof());
    const auto exited = engine.seen_of<wh::Exited>();
    CHECK(std::ranges::any_of(exited, [](const wh::Exited& e) { return e.role == wh::ProcessRole::Game; }));
    std::string printed;
    for (const wh::Output& output : engine.seen_of<wh::Output>())
        if (output.role == wh::ProcessRole::Game) printed.append(output.bytes.begin(), output.bytes.end());
    CHECK(printed.find("-epicapp=Fortnite") != std::string::npos);

    const auto exit = winhost.child->exit_within(kExit);
    REQUIRE(exit);
    CHECK(exit->code == kStopped);
    CHECK(wait_until_gone(dir, kGameName, kExit));
}

// umu-run, pressure-vessel and Proton sit between the runner and winhost, so this proves the token
// file in its read-only share reaches winhost inside the container and the game still starts.
TEST_CASE("under umu and GE-Proton winhost reads its token file and runs the game until a Stop", "[umu]") {
    const std::optional<UmuInputs> in = umu_inputs();
    if (!in) SKIP(kNoUmu);
    Logger::add_sink(std::make_unique<StderrSink>());
    Logger::install(1 << 20);
    Logger::set_level(LogLevel::Info);
    OsRandom random;
    auto scratch = testing::ScratchDir::create(random, "reboot-umu-gate");
    REQUIRE(scratch);
    const NativePath prefix_dir = scratch->path() / "prefix";
    Winhost winhost;
    LinuxRunnerPlatform runner{winhost.launcher, base_env(), scratch->path() / "umu", token_base(scratch->path())};
    const auto layout = runner.layout(ports::RunnerKind::Umu, {in->proton, in->launcher});
    REQUIRE(layout);

    const auto runtime = runner.runtime_setup(*layout, CancelToken{});
    Logger::flush();
    INFO((runtime ? std::string() : runtime.error().id));
    REQUIRE(runtime);
    REQUIRE(*runtime);
    {
        const auto boot = runner.prefix_command(*layout, prefix_dir, {ports::PrefixVerb::Boot, {}, {}}, umu_env(*layout, in->game));
        REQUIRE(boot);
        auto spawned = winhost.launcher.spawn(*boot);
        REQUIRE(spawned);
        Child wineboot(std::move(*spawned));
        const auto exit = wineboot.exit_within(kUmuSetup);
        INFO(wineboot.output());
        REQUIRE(exit);
        REQUIRE(exit->code == 0);
    }

    Loopback engine;
    Loopback game_channel;
    REQUIRE(engine.port() != 0);
    REQUIRE(game_channel.port() != 0);
    const std::array<u8, 32> game_token = fresh_token();
    REQUIRE(winhost.start_under(runner, *layout, prefix_dir, in->winhost, engine, umu_env(*layout, in->game)) == "");
    // The umu job gives the container a private $XDG_RUNTIME_DIR, which pressure-vessel does not share.
    if (const auto xdg = env_value("XDG_RUNTIME_DIR"))
        CHECK(winhost.token_path.string().starts_with(*xdg + "/reboot/ctl-"));
    const std::string launched = launch_game(engine, winhost, spawn_game(in->game, game_env_block(game_channel, game_token)));
    INFO(winhost.child->output());
    REQUIRE(launched == "");
    // winhost read it inside the container before its Hello, and the game finds nothing.
    CHECK_FALSE(fs::exists(winhost.token_path.parent_path()));
    expect_game_hello(game_channel, game_token);

    engine.send(wh::Stop{2, 500});
    const auto stopped = engine.expect<common::CommandResult>();
    REQUIRE(stopped);
    CHECK(stopped->ok);
    CHECK(engine.drain_to_eof());
    const auto exited = engine.seen_of<wh::Exited>();
    CHECK(std::ranges::any_of(exited, [](const wh::Exited& e) { return e.role == wh::ProcessRole::Game; }));
    // umu-run waits for Proton's wineserver, which lingers a few seconds after the last process.
    CHECK(winhost.child->exit_within(kWineStart).has_value());
}

TEST_CASE("a failed launch under Wine ends winhost with LaunchFailed", "[wine]") {
    const std::optional<Inputs> in = inputs();
    if (!in) SKIP(kNoWine);
    const NativePath& dir = prefix(*in);
    Loopback engine;
    Winhost winhost;
    REQUIRE(winhost.start(*in, dir, engine) == "");

    const bool hello = engine.accept_peer(kWineStart) && engine.preamble_ok() && engine.expect<wh::WhHello>();
    INFO(winhost.child->output());
    REQUIRE(hello);
    winhost.connected();
    engine.send(wh::WhWelcome{spawn_game(in->game.parent_path() / "missing-game.exe", utf16(std::string(2, '\0')))});
    const auto fatal = engine.expect<wh::WhFatal>(kWineStart);
    REQUIRE(fatal);
    CHECK(engine.drain_to_eof());

    const auto exit = winhost.child->exit_within(kExit);
    REQUIRE(exit);
    CHECK(exit->code == kLaunchFailed);
}

TEST_CASE("under Wine the game's host cmdline and environ hold no secret, and engine EOF kills its Job", "[wine]") {
    const std::optional<Inputs> in = inputs();
    if (!in) SKIP(kNoWine);
    const NativePath& dir = prefix(*in);
    Loopback engine;
    Loopback game_channel;
    Winhost winhost;
    const std::array<u8, 32> game_token = fresh_token();
    REQUIRE(winhost.start(*in, dir, engine) == "");
    const std::string launched = launch_game(engine, winhost, spawn_game(in->game, game_env_block(game_channel, game_token)));
    INFO(winhost.child->output());
    REQUIRE(launched == "");
    expect_game_hello(game_channel, game_token);

    const std::optional<u32> game = wait_for_process(dir, kGameName, kExit);
    REQUIRE(game);
    const NativePath proc = NativePath("/proc") / std::to_string(*game);
    for (const char* file : {"cmdline", "environ"}) {
        const std::string text = read_all(proc / file);
        INFO(file);
        REQUIRE_FALSE(text.empty());
        CHECK(text.find("-AUTH_PASSWORD") == std::string::npos);
        CHECK(text.find(kPassword) == std::string::npos);
        CHECK(text.find(gc::kEnvCtlToken) == std::string::npos);
        CHECK(text.find(base64url(game_token)) == std::string::npos);
        CHECK(text.find(base64url(winhost.token)) == std::string::npos);
        // Nor the token file's path, which only winhost's own argv names.
        CHECK(text.find(winhost.token_path.parent_path().filename().string()) == std::string::npos);
    }
    CHECK_FALSE(fs::exists(winhost.token_path.parent_path()));

    // The game keeps its own channel open, so only the Job kill can end it.
    engine.close_peer();
    const auto exit = winhost.child->exit_within(kExit);
    REQUIRE(exit);
    CHECK(exit->code == kEngineClosed);
    CHECK(wait_until_gone(dir, kGameName, kExit));
}

TEST_CASE("an engine killed with SIGKILL under Wine takes winhost and the game with it", "[wine]") {
    const std::optional<Inputs> in = inputs();
    if (!in) SKIP(kNoWine);
    const NativePath& dir = prefix(*in);
    Loopback game_channel;
    REQUIRE(game_channel.port() != 0);
    const std::array<u8, 32> game_token = fresh_token();
    std::array<int, 2> report{};
    REQUIRE(::pipe(report.data()) == 0);

    // The engine is a child process of its own; this process has no other thread to fork with.
    const pid_t engine_pid = ::fork();
    REQUIRE(engine_pid >= 0);
    if (engine_pid == 0) {
        ::close(report[0]);
        std::string line;
        {
            Loopback engine;
            Winhost winhost;
            line = winhost.start(*in, dir, engine);
            if (line.empty()) line = launch_game(engine, winhost, spawn_game(in->game, game_env_block(game_channel, game_token)));
            line = line.empty() ? "ready\n" : "error " + line + "\n";
            if (::write(report[1], line.data(), line.size()) < 0) ::_exit(1);
            // Held, with winhost and its channel, until the SIGKILL.
            if (line == "ready\n")
                for (;;) ::pause();
        }
        ::_exit(1);
    }
    ::close(report[1]);
    struct Reap {
        pid_t pid;
        ~Reap() {
            ::kill(pid, SIGKILL);
            ::waitpid(pid, nullptr, 0);
        }
    } reap{engine_pid};

    std::string line;
    pollfd entry{report[0], POLLIN, 0};
    while (::poll(&entry, 1, static_cast<int>(std::chrono::milliseconds(kWineStart * 2).count())) == 1) {
        char c = 0;
        if (::read(report[0], &c, 1) != 1 || c == '\n') break;
        line += c;
    }
    ::close(report[0]);
    REQUIRE(line == "ready");
    expect_game_hello(game_channel, game_token);
    REQUIRE(wait_for_process(dir, kWinhostName, kExit));
    REQUIRE(wait_for_process(dir, kGameName, kExit));

    ::kill(engine_pid, SIGKILL);
    CHECK(wait_until_gone(dir, kWinhostName, kExit));
    CHECK(wait_until_gone(dir, kGameName, kExit));
}
