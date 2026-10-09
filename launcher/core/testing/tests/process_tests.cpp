#include <chrono>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <catch2/catch_message.hpp>
#include <catch2/catch_test_macros.hpp>

#include "reboot/contracts/common.hpp"
#include "reboot/ports/process.hpp"
#include "reboot/testing/deterministic_runtime.hpp"
#include "reboot/testing/fake_platform_paths.hpp"
#include "reboot/testing/manual_waiter.hpp"
#include "reboot/testing/port_conformance.hpp"
#include "reboot/testing/scripted_process_launcher.hpp"

using namespace reboot;
using namespace reboot::testing;
using namespace std::chrono_literals;

namespace {

[[nodiscard]] std::span<const u8> text(std::string_view value) {
    return {reinterpret_cast<const u8*>(value.data()), value.size()};
}

[[nodiscard]] ports::ProcessLaunch launch_of(std::string_view exe) {
    ports::ProcessLaunch launch;
    launch.exe = default_fake_root() / "bin" / std::string(exe);
    return launch;
}

// Peers that behave as the shell one-liners the OS packages give the process suite.
class ExitPeer final : public IStdioPeer {
public:
    explicit ExitPeer(int code) : code_(code) {}
    void start(StdioPeerOutputs outputs) override { outputs.exit(code_); }
    void on_stdin(std::span<const u8>) override {}
    void on_stdin_eof() override {}

private:
    int code_;
};

class EchoPeer final : public IStdioPeer {
public:
    void start(StdioPeerOutputs outputs) override { outputs_ = std::move(outputs); }
    void on_stdin(std::span<const u8> bytes) override { outputs_.stdout_bytes(bytes); }
    void on_stdin_eof() override { outputs_.exit(0); }

private:
    StdioPeerOutputs outputs_;
};

class StderrPeer final : public IStdioPeer {
public:
    void start(StdioPeerOutputs outputs) override {
        outputs.stderr_line("something went to stderr");
        outputs.exit(0);
    }
    void on_stdin(std::span<const u8>) override {}
    void on_stdin_eof() override {}
};

class MarkerPeer final : public IStdioPeer {
public:
    explicit MarkerPeer(ports::ProcessLaunch launch) : launch_(std::move(launch)) {}
    void start(StdioPeerOutputs outputs) override {
        std::string marker;
        for (const auto& [name, value] : launch_.env.vars)
            if (name == "REBOOT_CONFORMANCE_MARKER") marker = value;
        const std::u8string cwd = launch_.cwd.u8string();
        const std::string out = marker + "\r\n" + std::string(cwd.begin(), cwd.end()) + "\n";
        outputs.stdout_bytes(text(out));
        outputs.exit(0);
    }
    void on_stdin(std::span<const u8>) override {}
    void on_stdin_eof() override {}

private:
    ports::ProcessLaunch launch_;
};

// Its grandchild is an orphan entry that dies with it, as a Job or process group takes it.
class TreePeer final : public IStdioPeer {
public:
    TreePeer(ScriptedProcessLauncher& launcher, u32 grandchild, std::chrono::system_clock::time_point created)
        : launcher_(launcher), grandchild_(grandchild), created_(created) {}
    ~TreePeer() override { (void)launcher_.kill(grandchild_, created_); }
    void start(StdioPeerOutputs outputs) override { outputs.stdout_bytes(text(std::to_string(grandchild_) + "\n")); }
    void on_stdin(std::span<const u8>) override {}
    void on_stdin_eof() override {}

private:
    ScriptedProcessLauncher& launcher_;
    u32 grandchild_;
    std::chrono::system_clock::time_point created_;
};

}  // namespace

TEST_CASE("ScriptedProcessLauncher passes the process suite", "[testing][conformance][process]") {
    DeterministicRuntime runtime;
    ManualWaiter waiter(runtime);
    ScriptedProcessLauncher launcher(runtime.strand(), runtime.clock(), FakeOs::Linux);
    constexpr u32 kGrandchild = 90001;
    const auto grandchild_created = runtime.clock().system_now();

    launcher.serve_exe("seven", [](const ports::ProcessLaunch&) { return std::make_unique<ExitPeer>(7); });
    launcher.serve_exe("echo", [](const ports::ProcessLaunch&) { return std::make_unique<EchoPeer>(); });
    launcher.serve_exe("stderr", [](const ports::ProcessLaunch&) { return std::make_unique<StderrPeer>(); });
    launcher.serve_exe("marker", [](const ports::ProcessLaunch& launch) { return std::make_unique<MarkerPeer>(launch); });
    launcher.serve_exe("tree", [&](const ports::ProcessLaunch&) {
        launcher.add_orphan(kGrandchild, grandchild_created);
        return std::make_unique<TreePeer>(launcher, kGrandchild, grandchild_created);
    });

    ProcessConformanceSubject subject;
    subject.exits_with_7 = launch_of("seven");
    subject.echoes_stdin = launch_of("echo");
    subject.writes_stderr = launch_of("stderr");
    subject.prints_marker_and_cwd = launch_of("marker");
    subject.spawns_grandchild = launch_of("tree");
    subject.process_exists = [&](u32 pid) {
        const auto alive = launcher.is_alive(pid, grandchild_created);
        return alive && *alive;
    };
    const ConformanceReport report =
        run_process_launcher_conformance(launcher, std::move(subject), {waiter, default_fake_root() / "scratch"});
    INFO(report.describe());
    CHECK(report.passed());
    CHECK(launcher.killed().size() >= 2);
}

TEST_CASE("an unscripted spawn fails and rules apply in order", "[testing][process]") {
    DeterministicRuntime runtime;
    ScriptedProcessLauncher launcher(runtime.strand(), runtime.clock(), FakeOs::Windows);
    const auto unscripted = launcher.spawn(launch_of("reboot-backend.exe"));
    REQUIRE_FALSE(unscripted);
    CHECK(unscripted.error().id == "testing.unscripted_spawn");

    int first = 0;
    launcher.add_rule({[](const ports::ProcessLaunch& l) { return l.exe.filename() == "game.exe"; },
                       [&first](ScriptedChild&) -> Result<void> {
                           ++first;
                           return {};
                       },
                       true});
    launcher.fail_exe("game.exe", make_diag(ErrorDomain::Internal, MessageId{"internal.bug"}));
    CHECK(launcher.spawn(launch_of("game.exe")));
    CHECK_FALSE(launcher.spawn(launch_of("game.exe")));
    CHECK(first == 1);
    CHECK(launcher.children().size() == 1);
}

TEST_CASE("a scripted child records stdin and exits once", "[testing][process]") {
    DeterministicRuntime runtime;
    ScriptedProcessLauncher launcher(runtime.strand(), runtime.clock(), FakeOs::Windows);
    launcher.on_exe("reboot-backend.exe", [](ScriptedChild& child) -> Result<void> {
        child.write_stderr_line("starting");
        return {};
    });
    auto handle = launcher.spawn(launch_of("reboot-backend.exe"));
    REQUIRE(handle);
    ScriptedChild* child = launcher.last("reboot-backend.exe");
    REQUIRE(child != nullptr);
    CHECK(child->pid() == (*handle)->pid());
    CHECK(child->created() == (*handle)->created());

    std::string err;
    std::vector<ports::ChildExit> exits;
    std::string out;
    (*handle)->on_stderr([&](std::span<const u8> bytes) { err.append(bytes.begin(), bytes.end()); });
    (*handle)->on_stdout([&](std::span<const u8> bytes) { out.append(bytes.begin(), bytes.end()); });
    (*handle)->on_exit([&](ports::ChildExit exit) { exits.push_back(exit); });
    runtime.run_until_idle();
    CHECK(err == "starting\n");

    (*handle)->write_stdin(encode_contract_frame(contracts::common::Ping{9}));
    CHECK(child->stdin_frames().count(contract_frame_type_v<contracts::common::Ping>) == 1);
    child->send(contracts::common::Pong{9});
    runtime.run_until_idle();
    CHECK(out.size() > 0);

    REQUIRE((*handle)->terminate_tree());
    REQUIRE((*handle)->terminate_tree());
    child->exit({0, std::nullopt});
    runtime.run_until_idle();
    REQUIRE(exits.size() == 1);
    CHECK(exits[0].code == kJobKillExitCode);
    CHECK(child->terminated());

    child->write_stdout(text("after exit"));
    runtime.run_until_idle();
    CHECK(out.find("after exit") == std::string::npos);
    handle->reset();
    CHECK(child->released());
}

TEST_CASE("a POSIX-shaped kill reports signal 9 and kill honours the creation time", "[testing][process]") {
    DeterministicRuntime runtime;
    ScriptedProcessLauncher launcher(runtime.strand(), runtime.clock(), FakeOs::MacOs);
    launcher.on_exe("server", [](ScriptedChild&) -> Result<void> { return {}; });
    auto handle = launcher.spawn(launch_of("server"));
    REQUIRE(handle);
    std::optional<ports::ChildExit> exit;
    (*handle)->on_exit([&](ports::ChildExit e) { exit = e; });

    const u32 pid = (*handle)->pid();
    const auto created = (*handle)->created();
    CHECK(*launcher.is_alive(pid, created));
    CHECK_FALSE(*launcher.is_alive(pid, created + 1s));
    CHECK_FALSE(launcher.kill(pid, created + 1s));
    REQUIRE(launcher.kill(pid, created));
    runtime.run_until_idle();
    REQUIRE(exit);
    CHECK(exit->signal == 9);
    CHECK_FALSE(exit->code);
    CHECK(launcher.killed() == std::vector<u32>{pid});

    launcher.add_orphan(4321, created);
    CHECK(*launcher.is_alive(4321, created));
    REQUIRE(launcher.kill(4321, created));
    CHECK_FALSE(*launcher.is_alive(4321, created));
}
