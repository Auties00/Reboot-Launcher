#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <condition_variable>
#include <deque>
#include <filesystem>
#include <fstream>
#include <future>
#include <memory>
#include <mutex>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <variant>
#include <vector>

#include "messages.hpp"
#include "reboot/foundation/cancel.hpp"
#include "reboot/foundation/clock.hpp"
#include "reboot/foundation/executor.hpp"
#include "reboot/foundation/log.hpp"
#include "reboot/foundation/random.hpp"
#include "reboot/os_linux/runner/linux_runner_platform.hpp"
#include "reboot/os_linux/runner/slr_build.hpp"
#include "reboot/os_linux/runner/slr_setup.hpp"
#include "reboot/os_linux/runner/umu_invocation.hpp"
#include "reboot/testing/scratch_dir.hpp"
#include "reboot/testing/scripted_process_launcher.hpp"

// SlrSetup on the real filesystem with a scripted umu-run, for the exits and races a real one
// cannot be made to produce on demand.

namespace fs = std::filesystem;
using rb::NativePath;
using rb::os_linux::runner::LinuxRunnerPlatform;
using rb::os_linux::runner::SlrBuild;
using rb::os_linux::runner::SlrSetup;
using rb::os_linux::runner::UmuInvocation;
using rb::ports::ChildExit;
using rb::testing::ScriptedChild;
using SpawnResult = rb::Result<void>;
using namespace std::chrono_literals;

namespace {

constexpr std::string_view kToolManifest = "\"manifest\"\n{\n  \"require_tool_appid\" \"1628350\"\n}\n";
constexpr std::string_view kVersions = "depot\t0.20240806.99006\t-\t-\n";

// Delivers on the posting thread, so the blocked setup hears its child without a second thread.
class InlineIo final : public rb::Executor {
public:
    void post(rb::UniqueFunction<void()> task) override { task(); }
    void post_at(rb::SteadyTime, rb::UniqueFunction<void()> task) override { task(); }
};

// Inline, but holds what the child sends during its spawn until the parent registers a callback,
// so output written at spawn is heard.
class HeldIo final : public rb::Executor {
public:
    void post(rb::UniqueFunction<void()> task) override {
        held_.push_back(std::move(task));
        if (holding) return;
        std::vector<rb::UniqueFunction<void()>> ready = std::exchange(held_, {});
        for (rb::UniqueFunction<void()>& each : ready) each();
    }
    void post_at(rb::SteadyTime, rb::UniqueFunction<void()> task) override { post(std::move(task)); }

    bool holding = false;

private:
    std::vector<rb::UniqueFunction<void()>> held_;
};

// The launcher's I/O thread: deliveries reach the blocked setup from another thread.
class IoThread final : public rb::Executor {
public:
    IoThread() : thread_([this] { run(); }) {}
    ~IoThread() override {
        {
            const std::scoped_lock lock(mutex_);
            stopping_ = true;
        }
        ready_.notify_all();
        thread_.join();
    }
    IoThread(const IoThread&) = delete;
    IoThread& operator=(const IoThread&) = delete;

    void post(rb::UniqueFunction<void()> task) override {
        {
            const std::scoped_lock lock(mutex_);
            tasks_.push_back(std::move(task));
        }
        ready_.notify_one();
    }
    void post_at(rb::SteadyTime, rb::UniqueFunction<void()> task) override { post(std::move(task)); }

private:
    void run() {
        std::unique_lock lock(mutex_);
        while (true) {
            ready_.wait(lock, [this] { return stopping_ || !tasks_.empty(); });
            if (tasks_.empty()) return;
            rb::UniqueFunction<void()> task = std::move(tasks_.front());
            tasks_.pop_front();
            lock.unlock();
            task();
            lock.lock();
        }
    }

    std::mutex mutex_;
    std::condition_variable ready_;
    std::deque<rb::UniqueFunction<void()>> tasks_;
    bool stopping_ = false;
    std::thread thread_;
};

// Keeps the Wine records; sinks stay installed for the whole process.
class WineLines final : public rb::LogSink {
public:
    struct Lines {
        std::mutex mutex;
        std::vector<std::string> texts;
    };

    explicit WineLines(std::shared_ptr<Lines> lines) : lines_(std::move(lines)) {}

    void write(std::span<const rb::LogRecord> records) override {
        const std::scoped_lock lock(lines_->mutex);
        for (const rb::LogRecord& record : records)
            if (record.category == rb::LogCategory::Wine) lines_->texts.push_back(record.text);
    }
    void flush() override {}

private:
    std::shared_ptr<Lines> lines_;
};

void write_file(const NativePath& path, std::string_view text) {
    fs::create_directories(path.parent_path());
    std::ofstream{path} << text;
}

rb::testing::ScratchDir scratch() {
    rb::OsRandom random;
    auto dir = rb::testing::ScratchDir::create(random, "slr-setup");
    REQUIRE(dir);
    return std::move(*dir);
}

// A GE-Proton root with its tool manifest; nothing is executed, so no file needs to run.
struct Runtime {
    explicit Runtime(const NativePath& dir) : folders(dir / "folders") {
        write_file(dir / "proton" / "toolmanifest.vdf", kToolManifest);
        layout = UmuInvocation{dir / "umu" / UmuInvocation::kUmuRun, dir / "proton", folders}.to_runtime_layout();
    }

    [[nodiscard]] NativePath scratch_prefix() const { return folders / SlrSetup::kScratchPrefix; }
    void install_build() const { write_file(folders / "steamrt3" / "VERSIONS.txt", kVersions); }

    NativePath folders;
    rb::ports::RuntimeLayout layout;
};

rb::ports::EnvBlock base_env() { return rb::ports::EnvBlock{{{"HOME", "/home/u"}}}; }

std::string_view var(const rb::ports::EnvBlock& env, std::string_view name) {
    for (const auto& [key, value] : env.vars)
        if (key == name) return value;
    return {};
}

// wineserver -k finds nothing to stop.
void stop_ok(rb::testing::ScriptedProcessLauncher& processes) {
    processes.on_exe("wineserver", [](ScriptedChild& child) -> SpawnResult {
        child.exit(ChildExit{0, std::nullopt});
        return {};
    });
}

struct Fixture {
    rb::ManualClock clock;
    InlineIo io;
    rb::testing::ScriptedProcessLauncher processes{io, clock, rb::testing::FakeOs::Linux};
    rb::testing::ScratchDir dir = scratch();
    Runtime runtime{dir.path()};
    SlrSetup setup{processes, base_env(), runtime.folders};

    Fixture() { stop_ok(processes); }

    // umu-run ends with `exit`, after installing the runtime build when `install` is set.
    void umu_exits(ChildExit exit, bool install) {
        processes.on_exe("umu-run", [this, exit, install](ScriptedChild& child) -> SpawnResult {
            if (install) runtime.install_build();
            child.write_stderr_line("umu: setting up the runtime");
            child.exit(exit);
            return {};
        });
    }
};

}  // namespace

TEST_CASE("the setup runs umu-run with the runtime update on and a fresh scratch prefix", "[slr_setup]") {
    Fixture fixture;
    write_file(fixture.runtime.scratch_prefix() / "stale", "left by a killed run");
    bool fresh_prefix = false;
    fixture.processes.on_exe("umu-run", [&](ScriptedChild& child) -> SpawnResult {
        fresh_prefix = fs::is_directory(fixture.runtime.scratch_prefix()) &&
                       !fs::exists(fixture.runtime.scratch_prefix() / "stale");
        fixture.runtime.install_build();
        child.exit(ChildExit{0, std::nullopt});
        return {};
    });

    const auto build = fixture.setup.run(fixture.runtime.layout, {});
    REQUIRE(build);
    CHECK(*build == SlrBuild{"steamrt3", "0.20240806.99006"});
    CHECK(fresh_prefix);
    CHECK_FALSE(fs::exists(fixture.runtime.scratch_prefix()));

    const ScriptedChild* umu = fixture.processes.last("umu-run");
    REQUIRE(umu);
    const rb::ports::ProcessLaunch& launch = umu->launch();
    CHECK(launch.exe == fixture.runtime.layout.entry);
    CHECK(launch.args == std::vector<std::string>{""});
    CHECK(launch.cwd == fixture.runtime.folders);
    CHECK(launch.stdio == rb::ports::StdioMode::Capture);
    CHECK(launch.own_group);
    CHECK(launch.scope_name == std::optional<std::string>{std::string(SlrSetup::kScopeName)});
    CHECK(var(launch.env, "HOME") == "/home/u");
    CHECK(var(launch.env, "UMU_RUNTIME_UPDATE") == "1");
    CHECK(var(launch.env, "WINEPREFIX") == fixture.runtime.scratch_prefix().string());
    CHECK(var(launch.env, "UMU_FOLDERS_PATH") == fixture.runtime.folders.string());
    CHECK(var(launch.env, "PROTONPATH") == fixture.runtime.layout.root.string());

    const ScriptedChild* stop = fixture.processes.last("wineserver");
    REQUIRE(stop);
    CHECK(stop->launch().exe == fixture.runtime.layout.root / "files/bin/wineserver");
    CHECK(stop->launch().args == std::vector<std::string>{"-k"});
    CHECK(var(stop->launch().env, "WINEPREFIX") == (fixture.runtime.scratch_prefix() / "pfx").string());
}

TEST_CASE("an umu-run that cannot start is reported and leaves nothing to stop", "[slr_setup]") {
    Fixture fixture;
    fixture.processes.fail_exe("umu-run", rb::internal_bug("no umu-run"));

    const auto build = fixture.setup.run(fixture.runtime.layout, {});
    REQUIRE_FALSE(build);
    CHECK(build.error().is(rb::os_linux::runner::kSlrSetupNotStarted));
    REQUIRE(build.error().causes.size() == 1);
    CHECK(build.error().causes.front().domain == rb::ErrorDomain::Internal);
    CHECK(fixture.processes.last("wineserver") == nullptr);
    CHECK_FALSE(fs::exists(fixture.runtime.scratch_prefix()));
}

TEST_CASE("a failing umu-run is retryable and names its exit code", "[slr_setup]") {
    Fixture fixture;
    fixture.umu_exits(ChildExit{3, std::nullopt}, false);

    const auto build = fixture.setup.run(fixture.runtime.layout, {});
    REQUIRE_FALSE(build);
    CHECK(build.error().is(rb::os_linux::runner::kSlrSetupFailed));
    CHECK(build.error().retryable);
    const rb::Arg* code = build.error().find_arg("exit_code");
    REQUIRE(code);
    CHECK(std::get<rb::i64>(*code) == 3);
    CHECK(fixture.processes.last("wineserver") != nullptr);
    CHECK_FALSE(fs::exists(fixture.runtime.scratch_prefix()));
}

TEST_CASE("an umu-run ended by a signal names it", "[slr_setup]") {
    Fixture fixture;
    fixture.umu_exits(ChildExit{std::nullopt, 6}, false);

    const auto build = fixture.setup.run(fixture.runtime.layout, {});
    REQUIRE_FALSE(build);
    CHECK(build.error().is(rb::os_linux::runner::kSlrSetupKilled));
    const rb::Arg* signal = build.error().find_arg("signal");
    REQUIRE(signal);
    CHECK(std::get<rb::i64>(*signal) == 6);
}

TEST_CASE("a clean exit without a runtime build is a read failure", "[slr_setup]") {
    Fixture fixture;
    fixture.umu_exits(ChildExit{0, std::nullopt}, false);

    const auto build = fixture.setup.run(fixture.runtime.layout, {});
    REQUIRE_FALSE(build);
    CHECK(build.error().is(rb::os_linux::runner::kRuntimeReadFailed));
}

TEST_CASE("a runtime build without a depot line is reported", "[slr_setup]") {
    Fixture fixture;
    fixture.processes.on_exe("umu-run", [&](ScriptedChild& child) -> SpawnResult {
        write_file(fixture.runtime.folders / "steamrt3" / "VERSIONS.txt", "#Name\tVersion\n");
        child.exit(ChildExit{0, std::nullopt});
        return {};
    });

    const auto build = fixture.setup.run(fixture.runtime.layout, {});
    REQUIRE_FALSE(build);
    CHECK(build.error().is(rb::os_linux::runner::kSlrBuildMissing));
}

TEST_CASE("a GE-Proton needing an unknown runtime is reported", "[slr_setup]") {
    Fixture fixture;
    write_file(fixture.runtime.layout.root / "toolmanifest.vdf", "\"manifest\" { \"require_tool_appid\" \"42\" }");
    fixture.umu_exits(ChildExit{0, std::nullopt}, true);

    const auto build = fixture.setup.run(fixture.runtime.layout, {});
    REQUIRE_FALSE(build);
    CHECK(build.error().is(rb::os_linux::runner::kSlrRuntimeUnknown));
}

TEST_CASE("a scratch prefix that cannot be created spawns nothing", "[slr_setup]") {
    Fixture fixture;
    write_file(fixture.runtime.folders, "a file where the folders should be");

    const auto build = fixture.setup.run(fixture.runtime.layout, {});
    REQUIRE_FALSE(build);
    CHECK(build.error().is(rb::os_linux::runner::kScratchPrefixFailed));
    CHECK(fixture.processes.children().empty());
}

TEST_CASE("an already cancelled setup spawns nothing", "[slr_setup]") {
    Fixture fixture;
    fixture.umu_exits(ChildExit{0, std::nullopt}, true);
    rb::CancelSource cancel;
    cancel.cancel(rb::CancelReason::User);

    const auto build = fixture.setup.run(fixture.runtime.layout, cancel.token());
    REQUIRE_FALSE(build);
    CHECK(build.error().is(rb::os_linux::runner::kSlrSetupCancelled));
    CHECK(build.error().kind == rb::ErrorKind::Cancelled);
    CHECK(fixture.processes.children().empty());
    CHECK_FALSE(fs::exists(fixture.runtime.scratch_prefix()));
}

TEST_CASE("a cancel during the spawn kills umu-run", "[slr_setup]") {
    Fixture fixture;
    rb::CancelSource cancel;
    fixture.processes.on_exe("umu-run", [&](ScriptedChild&) -> SpawnResult {
        cancel.cancel(rb::CancelReason::Shutdown);
        return {};
    });

    const auto build = fixture.setup.run(fixture.runtime.layout, cancel.token());
    REQUIRE_FALSE(build);
    CHECK(build.error().is(rb::os_linux::runner::kSlrSetupCancelled));
    const ScriptedChild* umu = fixture.processes.last("umu-run");
    REQUIRE(umu);
    CHECK(umu->terminated());
    CHECK(fixture.processes.last("wineserver") != nullptr);
    CHECK_FALSE(fs::exists(fixture.runtime.scratch_prefix()));
}

TEST_CASE("an exit heard before the cancel stands", "[slr_setup]") {
    Fixture fixture;
    rb::CancelSource cancel;
    fixture.processes.on_exe("umu-run", [&](ScriptedChild& child) -> SpawnResult {
        fixture.runtime.install_build();
        child.exit(ChildExit{0, std::nullopt});
        cancel.cancel(rb::CancelReason::User);
        return {};
    });

    const auto build = fixture.setup.run(fixture.runtime.layout, cancel.token());
    REQUIRE(build);
    CHECK(*build == SlrBuild{"steamrt3", "0.20240806.99006"});
    const ScriptedChild* umu = fixture.processes.last("umu-run");
    REQUIRE(umu);
    CHECK_FALSE(umu->terminated());
}

TEST_CASE("a cancel from another thread kills the running setup", "[slr_setup]") {
    const auto dir = scratch();
    const Runtime runtime{dir.path()};
    rb::ManualClock clock;
    IoThread io;
    rb::testing::ScriptedProcessLauncher processes{io, clock, rb::testing::FakeOs::Linux};
    stop_ok(processes);
    std::promise<void> spawned;
    processes.on_exe("umu-run", [&](ScriptedChild& child) -> SpawnResult {
        constexpr std::string_view kProgress = "downloading\n";
        child.write_stdout(std::span{reinterpret_cast<const rb::u8*>(kProgress.data()), kProgress.size()});
        spawned.set_value();
        return {};
    });
    SlrSetup setup{processes, base_env(), runtime.folders};
    rb::CancelSource cancel;

    auto running = std::async(std::launch::async, [&] { return setup.run(runtime.layout, cancel.token()); });
    REQUIRE(spawned.get_future().wait_for(10s) == std::future_status::ready);
    cancel.cancel(rb::CancelReason::User);
    REQUIRE(running.wait_for(10s) == std::future_status::ready);

    const auto build = running.get();
    REQUIRE_FALSE(build);
    CHECK(build.error().is(rb::os_linux::runner::kSlrSetupCancelled));
    const ScriptedChild* umu = processes.last("umu-run");
    REQUIRE(umu);
    CHECK(umu->terminated());
    CHECK_FALSE(fs::exists(runtime.scratch_prefix()));
}

TEST_CASE("the platform sets up an umu runtime through the scripted launcher", "[slr_setup]") {
    const auto dir = scratch();
    const Runtime runtime{dir.path()};
    rb::ManualClock clock;
    InlineIo io;
    rb::testing::ScriptedProcessLauncher processes{io, clock, rb::testing::FakeOs::Linux};
    stop_ok(processes);
    processes.on_exe("umu-run", [&](ScriptedChild& child) -> SpawnResult {
        runtime.install_build();
        child.exit(ChildExit{0, std::nullopt});
        return {};
    });
    LinuxRunnerPlatform platform{processes, base_env(), runtime.folders};

    CHECK(platform.runtime_setup(runtime.layout, {}));
    const ScriptedChild* umu = processes.last("umu-run");
    REQUIRE(umu);
    CHECK(var(umu->launch().env, "UMU_RUNTIME_UPDATE") == "1");
}

TEST_CASE("umu-run output without line breaks is logged in bounded lines", "[slr_setup]") {
    const auto lines = std::make_shared<WineLines::Lines>();
    rb::Logger::add_sink(std::make_unique<WineLines>(lines));
    rb::Logger::install(1 << 20);
    rb::Logger::set_level(rb::LogLevel::Info);
    const auto dir = scratch();
    const Runtime runtime{dir.path()};
    rb::ManualClock clock;
    HeldIo io;
    rb::testing::ScriptedProcessLauncher processes{io, clock, rb::testing::FakeOs::Linux};
    stop_ok(processes);
    const std::string progress(10000, '#');
    processes.on_exe("umu-run", [&](ScriptedChild& child) -> SpawnResult {
        runtime.install_build();
        io.holding = true;
        child.write_stdout(std::span{reinterpret_cast<const rb::u8*>(progress.data()), progress.size()});
        child.exit(ChildExit{0, std::nullopt});
        io.holding = false;
        return {};
    });
    SlrSetup setup{processes, base_env(), runtime.folders};

    REQUIRE(setup.run(runtime.layout, {}));
    rb::Logger::flush();
    const std::scoped_lock lock(lines->mutex);
    std::size_t logged = 0;
    for (const std::string& text : lines->texts) {
        if (text.empty() || text.find_first_not_of('#') != std::string::npos) continue;
        CHECK(text.size() <= 4096);
        logged += text.size();
    }
    CHECK(logged == progress.size());
}
