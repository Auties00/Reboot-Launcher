#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <filesystem>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "compat_test_support.hpp"
#include "reboot/compat/compat_document.hpp"
#include "reboot/compat/prefix_manager.hpp"
#include "reboot/compat/preferred_rhi_seed.hpp"
#include "reboot/foundation/executor.hpp"
#include "reboot/foundation/paths.hpp"
#include "reboot/storage/document_store.hpp"
#include "reboot/testing/fake_os.hpp"
#include "reboot/testing/fake_platform_paths.hpp"
#include "reboot/testing/fake_runner_platform.hpp"
#include "reboot/testing/in_memory_file_system.hpp"
#include "reboot/testing/scripted_process_launcher.hpp"
#include "test_data.hpp"

using namespace reboot;
using namespace reboot::compat;
using namespace std::chrono_literals;
using reboot::compat::test::DiskFileSystem;
using reboot::compat::test::read_file;
using reboot::compat::test::TestStrand;
using reboot::compat::test::write_file;

namespace {

namespace fs = std::filesystem;
using Args = std::vector<std::string>;

const Args kBoot{"wineboot", "-u"};
const Args kKill{"wineserver", "-k"};

[[nodiscard]] std::string value_of(const ports::EnvBlock& env, std::string_view name) {
    for (const auto& [key, value] : env.vars)
        if (key == name) return value;
    return {};
}

[[nodiscard]] bool has(const ports::EnvBlock& env, std::string_view name) {
    return std::ranges::any_of(env.vars, [&](const auto& var) { return var.first == name; });
}

struct Fixture {
    Fixture() {
        static_cast<void>(document.load());
        processes.add_rule(testing::SpawnRule{[](const ports::ProcessLaunch&) { return true; },
                                              [this](testing::ScriptedChild& child) { return play_wine(child); }, false});
    }

    ~Fixture() { workers.shutdown(); }

    // Plays the runner: wineboot leaves a usable prefix, the VC++ installer exits with
    // `installer_exit`, and a `hang` command never exits by itself.
    Result<void> play_wine(testing::ScriptedChild& child) {
        const ports::ProcessLaunch& launch = child.launch();
        commands.push_back(launch.args);
        launches.push_back(launch);
        if (hang) return {};
        const NativePath prefix(value_of(launch.env, "WINEPREFIX"));
        if (launch.args == kBoot) {
            if (boot_exit == 0) make_prefix(prefix);
            child.write_stderr_line("wine: configuration updated");
            child.exit({boot_exit, std::nullopt});
        } else if (launch.args == kKill) {
            child.exit({1, std::nullopt});
        } else {
            child.exit({installer_exit, std::nullopt});
        }
        return {};
    }

    static void make_prefix(const NativePath& prefix) {
        fs::create_directories(prefix / "drive_c" / "users" / "Public");
        fs::create_directories(prefix / "drive_c" / "users" / "steamuser" / "AppData" / "Local");
        fs::create_directories(prefix / "dosdevices");
        write_file(prefix / "system.reg", "WINE REGISTRY Version 2\n");
    }

    [[nodiscard]] PrefixRequest request(std::string runtime, std::string version) {
        PrefixRequest out;
        out.layout.root = root / "rt";
        out.layout.entry = root / "rt" / "bin" / "wine";
        out.runtime = RuntimeId{std::move(runtime)};
        out.runtime_version = std::move(version);
        out.env.vars = {{"HOME", "/home/player"}, {"LD_PRELOAD", "/evil.so"}, {"WINEPREFIX", "/elsewhere"}};
        out.game_dlls = game_dlls;
        out.vc_redist = [this](UniqueFunction<void(Result<components::PinnedRuntime>)> done) {
            ++vc_fetches;
            components::PinnedRuntime pinned;
            pinned.runtime.ref = {components::ComponentKind::Runtime, "vc-14", "14.40"};
            pinned.runtime.kind = components::RuntimeKind::VcRedist;
            pinned.runtime.root = root / "vc";
            strand.post([done = std::move(done), pinned = std::move(pinned)]() mutable { done(std::move(pinned)); });
        };
        return out;
    }

    [[nodiscard]] PrefixLease lease(RunnerKind kind = RunnerKind::Wine) {
        auto taken = manager.lease(kind, SessionId{Uuid{{static_cast<u8>(++sessions)}}});
        REQUIRE(taken);
        return std::move(*taken);
    }

    Result<PreparedPrefix> prepare(const PrefixLease& held, PrefixRequest wanted, CancelToken token = {}) {
        std::optional<Result<PreparedPrefix>> result;
        manager.prepare(held, std::move(wanted), std::move(token),
                        [this](const Progress& progress) { phases.emplace_back(progress.phase); },
                        [&](Result<PreparedPrefix> done) { result = std::move(done); });
        strand.run_until([&] { return result.has_value(); });
        return std::move(*result);
    }

    [[nodiscard]] std::optional<PrefixRecord> record(RunnerKind kind = RunnerKind::Wine) const {
        for (const PrefixRecord& entry : document.get().prefixes)
            if (entry.kind == kind) return entry;
        return std::nullopt;
    }

    [[nodiscard]] NativePath prefix(RunnerKind kind = RunnerKind::Wine) const { return *manager.prefix_dir(kind); }

    testing::ScratchDir scratch = test::make_scratch();
    NativePath root = scratch.path();
    ManualClock clock;
    TestStrand strand{clock};
    TimerService timers{clock, strand};
    WorkerPool workers{1};
    DiskFileSystem disk;
    testing::InMemoryFileSystem state_fs;
    storage::DocumentStore<CompatDocument> document{state_fs, workers, strand, clock, NativePath("/state/compat.json")};
    testing::ScriptedProcessLauncher processes{strand, clock, testing::FakeOs::Linux};
    testing::FakeRunnerPlatform runner{{RunnerKind::Umu, RunnerKind::Wine}};
    testing::FakePlatformPaths paths{root};
    AppLayout layout{DataRoot{root / "data", true}, paths};
    PrefixManager manager{PrefixManagerDeps{processes, runner, disk, workers, strand, timers, document}, layout};

    std::vector<NativePath> game_dlls;
    std::vector<Args> commands;
    std::vector<ports::ProcessLaunch> launches;
    std::vector<std::string> phases;
    int boot_exit = 0;
    int installer_exit = 0;
    bool hang = false;
    int vc_fetches = 0;
    int sessions = 0;
};

}  // namespace

TEST_CASE("only Wine runners have a prefix", "[compat][prefix_manager]") {
    Fixture f;
    const auto lease = f.manager.lease(RunnerKind::Native, SessionId{});
    REQUIRE_FALSE(lease);
    CHECK(lease.error().id == "compat.no_prefix");
    CHECK_FALSE(f.manager.prefix_dir(RunnerKind::Native));
    CHECK(f.manager.prefix_dir(RunnerKind::Umu)->filename() == "umu");
    CHECK(f.manager.prefix_dir(RunnerKind::MacRuntime)->filename() == "mac_runtime");
}

TEST_CASE("a missing prefix is booted through the runner with the runner layer", "[compat][prefix_manager]") {
    Fixture f;
    const PrefixLease lease = f.lease();
    const auto prepared = f.prepare(lease, f.request("kron-11", "11.0"));
    REQUIRE(prepared);
    CHECK(prepared->dir == f.prefix());
    CHECK(prepared->paths.devices().empty());
    CHECK(f.commands == std::vector<Args>{kBoot});

    const ports::EnvBlock& env = f.launches[0].env;
    CHECK(value_of(env, "WINEPREFIX") == f.prefix().string());
    CHECK(value_of(env, "HOME") == "/home/player");
    CHECK(value_of(env, "WINEDLLOVERRIDES") == "winemenubuilder.exe=d");
    CHECK(value_of(env, "WINEDEBUG") == "fixme-all");
    CHECK_FALSE(has(env, "LD_PRELOAD"));
    CHECK(f.record() == PrefixRecord{RunnerKind::Wine, RuntimeId{"kron-11"}, "11.0", false});
    CHECK(std::ranges::find(f.phases, std::string("prefix_boot")) != f.phases.end());
}

TEST_CASE("a prefix on the session's runtime is ready while other sessions use it", "[compat][prefix_manager]") {
    Fixture f;
    const PrefixLease first = f.lease();
    REQUIRE(f.prepare(first, f.request("kron-11", "11.0")));
    f.commands.clear();

    const PrefixLease second = f.lease();
    CHECK_FALSE(f.manager.idle(RunnerKind::Wine));
    REQUIRE(f.prepare(second, f.request("kron-11", "11.0")));
    CHECK(f.commands.empty());
}

TEST_CASE("a newer runtime upgrades the prefix in place after stopping its server", "[compat][prefix_manager]") {
    Fixture f;
    const PrefixLease lease = f.lease();
    REQUIRE(f.prepare(lease, f.request("kron-11", "11.0")));
    write_file(f.prefix() / "drive_c" / "marker", "kept");
    f.commands.clear();

    REQUIRE(f.prepare(lease, f.request("kron-11-2", "11.0.2")));
    CHECK(f.commands == std::vector<Args>{kKill, kBoot});
    CHECK(read_file(f.prefix() / "drive_c" / "marker") == "kept");
    CHECK(f.record()->runtime == RuntimeId{"kron-11-2"});
    CHECK_FALSE(fs::exists(f.prefix().parent_path() / "wine.backup-11.0"));
}

TEST_CASE("another runtime id at an equal version upgrades the prefix too", "[compat][prefix_manager]") {
    Fixture f;
    const PrefixLease lease = f.lease();
    REQUIRE(f.prepare(lease, f.request("kron-11", "11.0")));
    f.commands.clear();
    REQUIRE(f.prepare(lease, f.request("kron-11-rebuild", "11.0")));
    CHECK(f.commands == std::vector<Args>{kKill, kBoot});
}

TEST_CASE("an older runtime backs the prefix up first, replacing the previous backup", "[compat][prefix_manager]") {
    Fixture f;
    const PrefixLease lease = f.lease();
    REQUIRE(f.prepare(lease, f.request("kron-12", "12.0")));
    write_file(f.prefix() / "drive_c" / "marker", "from 12");
    const NativePath stale = f.prefix().parent_path() / "wine.backup-13.0";
    write_file(stale / "system.reg", "old");
    f.commands.clear();

    REQUIRE(f.prepare(lease, f.request("kron-11", "11.0")));
    CHECK(f.commands == std::vector<Args>{kKill, kBoot});
    const NativePath backup = f.prefix().parent_path() / "wine.backup-12.0";
    CHECK(read_file(backup / "drive_c" / "marker") == "from 12");
    CHECK(fs::exists(backup / "system.reg"));
    CHECK_FALSE(fs::exists(stale));
    CHECK(f.record()->runtime_version == "11.0");
}

TEST_CASE("a failed backup leaves the prefix untouched", "[compat][prefix_manager]") {
    Fixture f;
    const PrefixLease lease = f.lease();
    REQUIRE(f.prepare(lease, f.request("kron-12", "12.0")));
    write_file(f.prefix().parent_path() / "wine.backup-13.0" / "x", "old");
    f.disk.faults().fail_next(testing::FsOperation::RemoveTree, internal_bug("test"));
    f.commands.clear();

    const auto prepared = f.prepare(lease, f.request("kron-11", "11.0"));
    REQUIRE_FALSE(prepared);
    CHECK(prepared.error().id == "compat.prefix_backup_failed");
    CHECK(f.commands == std::vector<Args>{kKill});
    CHECK(f.record()->runtime_version == "12.0");
}

TEST_CASE("a backup that cannot replace the earlier one leaves both prefix and earlier backup", "[compat][prefix_manager]") {
    Fixture f;
    const PrefixLease lease = f.lease();
    REQUIRE(f.prepare(lease, f.request("kron-12", "12.0")));
    const NativePath earlier = f.prefix().parent_path() / "wine.backup-13.0";
    write_file(earlier / "system.reg", "old");
    f.disk.refused_removal = earlier;
    f.commands.clear();

    const auto prepared = f.prepare(lease, f.request("kron-11", "11.0"));
    REQUIRE_FALSE(prepared);
    CHECK(prepared.error().id == "compat.prefix_backup_failed");
    CHECK(read_file(earlier / "system.reg") == "old");
    CHECK_FALSE(fs::exists(f.prefix().parent_path() / "wine.backup-12.0"));
    CHECK_FALSE(fs::exists(f.prefix().parent_path() / "wine.backup.partial"));
    CHECK(fs::exists(f.prefix() / "system.reg"));
    CHECK(f.record()->runtime_version == "12.0");
}

TEST_CASE("a change waits for the prefix's other sessions to end", "[compat][prefix_manager]") {
    Fixture f;
    const PrefixLease mine = f.lease();
    REQUIRE(f.prepare(mine, f.request("kron-11", "11.0")));
    const PrefixLease other = f.lease();
    f.commands.clear();

    const auto busy = f.prepare(mine, f.request("kron-12", "12.0"));
    REQUIRE_FALSE(busy);
    CHECK(busy.error().id == "compat.prefix_busy");
    CHECK(busy.error().kind == ErrorKind::Conflict);
    CHECK(test::arg_text(busy.error(), "holder") == format_uuid(other.session().value));
    CHECK(f.commands.empty());
}

TEST_CASE("an unusable prefix is recreated and keeps the game's saved data", "[compat][prefix_manager]") {
    Fixture f;
    const NativePath old_saved = f.prefix() / "drive_c" / "users" / "player" / "AppData" / "Local" / "FortniteGame" / "Saved";
    write_file(old_saved / "Config" / "WindowsClient" / "GameUserSettings.ini", "[ScalabilityGroups]\n");
    fs::create_directories(f.prefix() / "drive_c" / "users" / "Public");

    const PrefixLease lease = f.lease();
    REQUIRE(f.prepare(lease, f.request("kron-11", "11.0")));
    CHECK(f.commands == std::vector<Args>{kKill, kBoot});
    const NativePath moved = f.prefix() / "drive_c" / "users" / "steamuser" / "AppData" / "Local" / "FortniteGame" /
                             "Saved" / "Config" / "WindowsClient" / "GameUserSettings.ini";
    CHECK(read_file(moved) == "[ScalabilityGroups]\n");
    CHECK_FALSE(fs::exists(f.prefix().parent_path() / "wine.old"));
    CHECK(fs::exists(f.prefix() / "system.reg"));
}

TEST_CASE("saved data set aside by an interrupted recreation is carried over later", "[compat][prefix_manager]") {
    Fixture f;
    const NativePath aside = f.prefix().parent_path() / "wine.old";
    const NativePath old_ini = aside / "drive_c" / "users" / "player" / "AppData" / "Local" / "FortniteGame" / "Saved" /
                               "Config" / "WindowsClient" / "GameUserSettings.ini";
    write_file(old_ini, "[ScalabilityGroups]\n");
    const NativePath moved = f.prefix() / "drive_c" / "users" / "steamuser" / "AppData" / "Local" / "FortniteGame" /
                             "Saved" / "Config" / "WindowsClient" / "GameUserSettings.ini";

    SECTION("with no prefix left") {}
    SECTION("with a partial prefix left") { fs::create_directories(f.prefix() / "drive_c" / "users" / "steamuser"); }

    const PrefixLease lease = f.lease();
    REQUIRE(f.prepare(lease, f.request("kron-11", "11.0")));
    CHECK(read_file(moved) == "[ScalabilityGroups]\n");
    CHECK_FALSE(fs::exists(aside));
}

TEST_CASE("a recreation whose boot fails keeps the saved data set aside", "[compat][prefix_manager]") {
    Fixture f;
    const NativePath saved_ini = f.prefix() / "drive_c" / "users" / "player" / "AppData" / "Local" / "FortniteGame" /
                                 "Saved" / "Config" / "WindowsClient" / "GameUserSettings.ini";
    write_file(saved_ini, "[ScalabilityGroups]\n");
    f.boot_exit = 3;
    const PrefixLease lease = f.lease();
    REQUIRE_FALSE(f.prepare(lease, f.request("kron-11", "11.0")));

    // The failed boot left a partial tree; the next attempt must not set it aside over the saved data.
    fs::create_directories(f.prefix() / "drive_c");
    f.boot_exit = 0;
    REQUIRE(f.prepare(lease, f.request("kron-11", "11.0")));
    const NativePath moved = f.prefix() / "drive_c" / "users" / "steamuser" / "AppData" / "Local" / "FortniteGame" /
                             "Saved" / "Config" / "WindowsClient" / "GameUserSettings.ini";
    CHECK(read_file(moved) == "[ScalabilityGroups]\n");
}

TEST_CASE("a boot that fails names its step", "[compat][prefix_manager]") {
    Fixture f;
    f.boot_exit = 3;
    const PrefixLease lease = f.lease();
    const auto prepared = f.prepare(lease, f.request("kron-11", "11.0"));
    REQUIRE_FALSE(prepared);
    CHECK(prepared.error().id == "compat.prefix_failed");
    CHECK(test::arg_text(prepared.error(), "step") == "boot");
    CHECK_FALSE(f.record());
}

TEST_CASE("a runner that refuses the command fails the step", "[compat][prefix_manager]") {
    Fixture f;
    f.runner.faults().fail_next(testing::RunnerOperation::PrefixCommand, internal_bug("test"));
    const PrefixLease lease = f.lease();
    const auto prepared = f.prepare(lease, f.request("kron-11", "11.0"));
    REQUIRE_FALSE(prepared);
    CHECK(test::arg_text(prepared.error(), "step") == "boot");
    REQUIRE(prepared.error().causes.size() == 1);
    CHECK(prepared.error().causes[0].id == "internal.bug");
}

TEST_CASE("a game DLL that imports the VC++ runtime seeds it once", "[compat][prefix_manager]") {
    Fixture f;
    f.game_dlls = {compat::test::data_path("pe64_dynamic_crt.dll")};
    const PrefixLease lease = f.lease();
    REQUIRE(f.prepare(lease, f.request("kron-11", "11.0")));
    REQUIRE(f.commands.size() == 2);
    CHECK(f.commands[1] == Args{(f.root / "vc" / "vc_redist.x64.exe").string(), "/install", "/quiet", "/norestart"});
    CHECK(f.vc_fetches == 1);
    CHECK(f.record()->vc_runtime_seeded);

    f.commands.clear();
    REQUIRE(f.prepare(lease, f.request("kron-11", "11.0")));
    CHECK(f.commands.empty());
    CHECK(f.vc_fetches == 1);
}

TEST_CASE("a DLL beside a game DLL that it imports counts too, whatever its case", "[compat][prefix_manager]") {
    Fixture f;
    const NativePath dir = f.root / "dlls";
    fs::create_directories(dir);
    fs::copy_file(compat::test::data_path("pe64_static_crt.dll"), dir / "boot.dll");
    fs::copy_file(compat::test::data_path("pe64_dynamic_crt.dll"), dir / "user32.DLL");
    f.game_dlls = {dir / "boot.dll"};
    const PrefixLease lease = f.lease();
    REQUIRE(f.prepare(lease, f.request("kron-11", "11.0")));
    CHECK(f.vc_fetches == 1);
}

TEST_CASE("a static CRT game needs no VC++ runtime", "[compat][prefix_manager]") {
    Fixture f;
    f.game_dlls = {compat::test::data_path("pe64_static_crt.dll")};
    const PrefixLease lease = f.lease();
    REQUIRE(f.prepare(lease, f.request("kron-11", "11.0")));
    CHECK(f.vc_fetches == 0);
    CHECK_FALSE(f.record()->vc_runtime_seeded);
}

TEST_CASE("a failed VC++ install keeps the upgraded prefix recorded", "[compat][prefix_manager]") {
    Fixture f;
    f.installer_exit = 1603;
    f.game_dlls = {compat::test::data_path("pe64_dynamic_crt.dll")};
    const PrefixLease lease = f.lease();
    const auto prepared = f.prepare(lease, f.request("kron-11", "11.0"));
    REQUIRE_FALSE(prepared);
    CHECK(prepared.error().id == "compat.vc_runtime_failed");
    CHECK(f.record() == PrefixRecord{RunnerKind::Wine, RuntimeId{"kron-11"}, "11.0", false});
}

TEST_CASE("a malformed game DLL fails the prepare", "[compat][prefix_manager]") {
    Fixture f;
    f.game_dlls = {compat::test::data_path("pe32.dll")};
    const PrefixLease lease = f.lease();
    const auto prepared = f.prepare(lease, f.request("kron-11", "11.0"));
    REQUIRE_FALSE(prepared);
    CHECK(prepared.error().id == "compat.pe_malformed");
    CHECK(f.commands.empty());
}

TEST_CASE("the macOS prefix plays DX11 under DXMT", "[compat][prefix_manager]") {
    Fixture f;
    const PrefixLease lease = f.lease(RunnerKind::MacRuntime);
    PrefixRequest wanted = f.request("mac-wine-11", "11.0");
    wanted.layout.env = {{"WINEDLLOVERRIDES", "dxgi,d3d11=n,b"}};
    REQUIRE(f.prepare(lease, std::move(wanted)));
    CHECK(value_of(f.launches[0].env, "WINEDLLOVERRIDES") == "winemenubuilder.exe=d");
    const NativePath ini = f.prefix(RunnerKind::MacRuntime) / "drive_c" / "users" / "steamuser" / "AppData" / "Local" /
                           "FortniteGame" / "Saved" / "Config" / "WindowsClient" / std::string(kGameUserSettingsFile);
    CHECK(read_file(ini) == "[D3DRHIPreference]\nPreferredRHI=dx11\n");
}

TEST_CASE("prepares of one prefix run one at a time", "[compat][prefix_manager]") {
    Fixture f;
    const PrefixLease lease = f.lease();
    std::optional<Result<PreparedPrefix>> one;
    std::optional<Result<PreparedPrefix>> two;
    f.manager.prepare(lease, f.request("kron-11", "11.0"), {}, nullptr, [&](Result<PreparedPrefix> r) { one = std::move(r); });
    f.manager.prepare(lease, f.request("kron-11", "11.0"), {}, nullptr, [&](Result<PreparedPrefix> r) {
        CHECK(one.has_value());
        two = std::move(r);
    });
    f.strand.run_until([&] { return one.has_value() && two.has_value(); });
    CHECK(*one);
    CHECK(*two);
    // The second found the prefix the first booted.
    CHECK(f.commands == std::vector<Args>{kBoot});
}

TEST_CASE("prepare never completes inside the call", "[compat][prefix_manager]") {
    Fixture f;
    const PrefixLease lease = f.lease();
    CancelSource cancel;
    cancel.cancel(CancelReason::User);
    std::optional<Result<PreparedPrefix>> result;
    f.manager.prepare(lease, f.request("kron-11", "11.0"), cancel.token(), nullptr,
                      [&](Result<PreparedPrefix> r) { result = std::move(r); });
    CHECK_FALSE(result.has_value());
    f.strand.run_until([&] { return result.has_value(); });
    REQUIRE_FALSE(*result);
    CHECK(result->error().kind == ErrorKind::Cancelled);
}

TEST_CASE("a cancelled command is killed and the prepare ends cancelled", "[compat][prefix_manager]") {
    Fixture f;
    f.hang = true;
    const PrefixLease lease = f.lease();
    CancelSource cancel;
    std::optional<Result<PreparedPrefix>> result;
    f.manager.prepare(lease, f.request("kron-11", "11.0"), cancel.token(), nullptr,
                      [&](Result<PreparedPrefix> r) { result = std::move(r); });
    f.strand.run_until([&] { return !f.processes.children().empty(); });
    f.strand.drain();
    CHECK_FALSE(f.manager.idle(RunnerKind::Wine));
    cancel.cancel(CancelReason::User);
    f.strand.run_until([&] { return result.has_value(); });
    REQUIRE_FALSE(*result);
    CHECK(result->error().kind == ErrorKind::Cancelled);
    CHECK(f.processes.children().back()->terminated());
}

TEST_CASE("a prefix is idle without leases and prepares", "[compat][prefix_manager]") {
    Fixture f;
    CHECK(f.manager.idle(RunnerKind::Wine));
    {
        const PrefixLease lease = f.lease();
        CHECK_FALSE(f.manager.idle(RunnerKind::Wine));
        CHECK(f.manager.idle(RunnerKind::Umu));
    }
    CHECK(f.manager.idle(RunnerKind::Wine));

    PrefixLease moved = f.lease();
    PrefixLease target = std::move(moved);
    CHECK_FALSE(moved.held());
    CHECK(target.held());
    target.release();
    CHECK(f.manager.idle(RunnerKind::Wine));
}
