#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "reboot/foundation/sha256.hpp"
#include "reboot/gameserver/game_server_binary.hpp"
#include "reboot/storage/document_store.hpp"
#include "reboot/testing/fake_game_server.hpp"
#include "reboot/testing/fake_platform_paths.hpp"
#include "reboot/testing/in_memory_file_system.hpp"
#include "reboot/testing/scripted_process_launcher.hpp"
#include "test_strand.hpp"

using namespace reboot;
using namespace reboot::gameserver;
using namespace std::chrono_literals;
namespace gs = reboot::contracts::game_server;

namespace {

constexpr std::string_view kExeName = "reboot-game-server";

struct DescribeScript {
    std::vector<u8> output;
    std::optional<int> exit_code = 0;
};

std::vector<u8> bytes_of(std::string_view text) { return {text.begin(), text.end()}; }

struct Fixture {
    Fixture() {
        fs.write_text(exe, "binary v1");
        static_cast<void>(cache.load());
        launcher.on_exe(kExeName, [this](testing::ScriptedChild& child) -> Result<void> {
            if (spawn_error) return std::unexpected(*spawn_error);
            if (!script.output.empty()) child.write_stdout(script.output);
            if (script.exit_code) child.exit(ports::ChildExit{*script.exit_code, std::nullopt});
            return {};
        });
        make();
    }

    void make() {
        binary = std::make_unique<GameServerBinary>(
            exe, fs, launcher, workers, strand, timers, clock, cache,
            [this]() -> Result<process::BuiltEnv> {
                ++env_calls;
                if (env_error) return std::unexpected(*env_error);
                return process::BuiltEnv{};
            },
            [this](const process::ChildRecord& child, process::RecordChange change) { records.emplace_back(child, change); },
            10s);
    }

    void describe_script(const gs::GameServerDescription& description) {
        script.output = encode_contract_frame(description);
        script.exit_code = 0;
    }

    [[nodiscard]] Result<DescribedBinary> describe() {
        std::optional<Result<DescribedBinary>> result;
        binary->describe([&result](Result<DescribedBinary> described) { result = std::move(described); });
        strand.run_until([&] { return result.has_value(); });
        return std::move(*result);
    }

    [[nodiscard]] Sha256Digest digest_of(std::string_view text) const { return sha256(bytes_of(text)); }

    [[nodiscard]] std::size_t spawns() const { return launcher.children().size(); }

    ManualClock clock;
    test::TestStrand strand{clock};
    TimerService timers{clock, strand};
    testing::InMemoryFileSystem fs;
    testing::ScriptedProcessLauncher launcher{strand, clock, testing::FakeOs::Linux};
    // Declared after what its jobs use, so it joins them before those go away.
    WorkerPool workers{1};
    storage::DocumentStore<DescribeCacheDocument> cache{fs, workers, strand, clock,
                                                        testing::default_fake_root() / "cache" / "game-server-describe.json"};
    NativePath exe = testing::default_fake_root() / "app" / std::string(kExeName);
    DescribeScript script{encode_contract_frame(testing::default_fake_description()), 0};
    std::optional<Diagnostic> spawn_error;
    std::optional<Diagnostic> env_error;
    int env_calls = 0;
    std::vector<std::pair<process::ChildRecord, process::RecordChange>> records;
    std::unique_ptr<GameServerBinary> binary;
};

}  // namespace

TEST_CASE("a cache miss runs --describe and caches the result", "[gameserver][binary]") {
    Fixture f;
    f.clock.set_system(std::chrono::system_clock::time_point{} + 1000h);
    CHECK(f.binary->current() == nullptr);

    Result<DescribedBinary> described = f.describe();
    REQUIRE(described.has_value());
    CHECK(described->exe == f.exe);
    CHECK(described->sha256 == f.digest_of("binary v1"));
    CHECK(same_description(described->description, testing::default_fake_description()));

    REQUIRE(f.spawns() == 1);
    const ports::ProcessLaunch& launch = f.launcher.children()[0]->launch();
    CHECK(launch.args == std::vector<std::string>{"--describe"});
    CHECK(launch.cwd == f.exe.parent_path());
    CHECK(launch.stdio == ports::StdioMode::Capture);
    CHECK(launch.own_group);
    CHECK(f.env_calls == 1);

    REQUIRE(f.records.size() == 2);
    CHECK(f.records[0].second == process::RecordChange::Spawned);
    CHECK(f.records[0].first.role == process::ChildRole::GameServer);
    CHECK(f.records[0].first.pid == f.launcher.children()[0]->pid());
    CHECK(f.records[1].second == process::RecordChange::Exited);

    const CachedDescription* cached = f.cache.get().find(described->sha256);
    REQUIRE(cached != nullptr);
    CHECK(cached->described_at == f.clock.system_now());
    REQUIRE(f.binary->current() != nullptr);
    CHECK(f.binary->current()->sha256 == described->sha256);
}

TEST_CASE("a cache hit does not run the binary; a changed binary is described again", "[gameserver][binary]") {
    Fixture f;
    REQUIRE(f.describe().has_value());
    REQUIRE(f.spawns() == 1);

    Result<DescribedBinary> again = f.describe();
    REQUIRE(again.has_value());
    CHECK(f.spawns() == 1);

    f.fs.write_text(f.exe, "binary v2");
    Result<DescribedBinary> replaced = f.describe();
    REQUIRE(replaced.has_value());
    CHECK(replaced->sha256 == f.digest_of("binary v2"));
    CHECK(f.spawns() == 2);
    CHECK(f.cache.get().entries.size() == 2);
}

TEST_CASE("a new GameServerBinary reuses the cache across restarts", "[gameserver][binary]") {
    Fixture f;
    REQUIRE(f.describe().has_value());
    f.make();
    REQUIRE(f.describe().has_value());
    CHECK(f.spawns() == 1);
}

TEST_CASE("describe is single flight", "[gameserver][binary]") {
    Fixture f;
    std::vector<Result<DescribedBinary>> results;
    for (int i = 0; i < 3; ++i)
        f.binary->describe([&results](Result<DescribedBinary> described) { results.push_back(std::move(described)); });
    f.strand.run_until([&] { return results.size() == 3; });
    CHECK(f.spawns() == 1);
    for (const Result<DescribedBinary>& result : results) {
        REQUIRE(result.has_value());
        CHECK(result->sha256 == f.digest_of("binary v1"));
    }
}

TEST_CASE("a describe that never answers times out and is killed", "[gameserver][binary]") {
    Fixture f;
    f.script = DescribeScript{{}, std::nullopt};
    std::optional<Result<DescribedBinary>> result;
    f.binary->describe([&result](Result<DescribedBinary> described) { result = std::move(described); });
    f.strand.run_until([&] { return f.spawns() == 1; });
    f.strand.advance(9s);
    CHECK_FALSE(result.has_value());
    f.strand.advance(1s);
    REQUIRE(result.has_value());
    REQUIRE_FALSE(result->has_value());
    CHECK(result->error().id == "gameserver.describe_timeout");
    CHECK(f.launcher.children()[0]->terminated());
    f.strand.run_ready();
    REQUIRE(f.records.size() == 2);
    CHECK(f.records[1].second == process::RecordChange::Exited);
    CHECK(f.binary->current() == nullptr);
}

TEST_CASE("an exit without output is describe_no_output", "[gameserver][binary]") {
    Fixture f;
    f.script = DescribeScript{{}, 3};
    Result<DescribedBinary> described = f.describe();
    REQUIRE_FALSE(described.has_value());
    CHECK(described.error().id == "gameserver.describe_no_output");
    CHECK(f.cache.get().entries.empty());
}

TEST_CASE("anything but exactly one description frame is malformed", "[gameserver][binary]") {
    Fixture f;
    SECTION("garbage") { f.script.output = bytes_of("not a frame at all"); }
    SECTION("a truncated frame") { f.script.output.resize(f.script.output.size() - 1); }
    SECTION("two frames") {
        const std::vector<u8> frame = f.script.output;
        f.script.output.insert(f.script.output.end(), frame.begin(), frame.end());
    }
    SECTION("another frame type") { f.script.output = encode_contract_frame(gs::PlayerCount{3}); }
    Result<DescribedBinary> described = f.describe();
    REQUIRE_FALSE(described.has_value());
    CHECK(described.error().id == "gameserver.describe_malformed");
}

TEST_CASE("the protocol and the sockets are checked", "[gameserver][binary]") {
    Fixture f;
    gs::GameServerDescription description = testing::default_fake_description();
    SECTION("protocol") {
        description.protocol = gs::kGameServerProtocol + 1;
        f.describe_script(description);
        Result<DescribedBinary> described = f.describe();
        REQUIRE_FALSE(described.has_value());
        CHECK(described.error().id == "gameserver.protocol_mismatch");
        CHECK(*described.error().find_arg("actual") == Arg{u64{gs::kGameServerProtocol + 1}});
    }
    SECTION("no game socket") {
        description.sockets = {gs::SocketSpec{gs::SocketRole::Beacon}};
        f.describe_script(description);
        Result<DescribedBinary> described = f.describe();
        REQUIRE_FALSE(described.has_value());
        CHECK(described.error().id == "gameserver.invalid_sockets");
        CHECK(*described.error().find_arg("actual") == Arg{u64{0}});
    }
    SECTION("two game sockets") {
        description.sockets = {gs::SocketSpec{gs::SocketRole::Game}, gs::SocketSpec{gs::SocketRole::Game}};
        f.describe_script(description);
        Result<DescribedBinary> described = f.describe();
        REQUIRE_FALSE(described.has_value());
        CHECK(described.error().id == "gameserver.invalid_sockets");
    }
    SECTION("a role outside the contract") {
        description.sockets.push_back(gs::SocketSpec{static_cast<gs::SocketRole>(7)});
        f.describe_script(description);
        Result<DescribedBinary> described = f.describe();
        REQUIRE_FALSE(described.has_value());
        CHECK(described.error().id == "gameserver.describe_malformed");
    }
    CHECK(f.cache.get().entries.empty());
}

TEST_CASE("a cached description is checked again", "[gameserver][binary]") {
    Fixture f;
    gs::GameServerDescription stale = testing::default_fake_description();
    stale.protocol = 99;
    REQUIRE(f.cache
                .update([&](DescribeCacheDocument& document) {
                    document.put(CachedDescription{f.digest_of("binary v1"), {}, stale});
                })
                .has_value());
    Result<DescribedBinary> described = f.describe();
    REQUIRE_FALSE(described.has_value());
    CHECK(described.error().id == "gameserver.protocol_mismatch");
    CHECK(f.spawns() == 0);
}

TEST_CASE("a missing exe is unreadable", "[gameserver][binary]") {
    Fixture f;
    f.exe = testing::default_fake_root() / "app" / "missing" / std::string(kExeName);
    f.make();
    Result<DescribedBinary> described = f.describe();
    REQUIRE_FALSE(described.has_value());
    CHECK(described.error().id == "gameserver.exe_unreadable");
    CHECK(described.error().causes.size() == 1);
    CHECK(f.spawns() == 0);
}

TEST_CASE("spawn and environment failures are describe_spawn_failed with the cause", "[gameserver][binary]") {
    Fixture f;
    SECTION("spawn") {
        f.spawn_error = make_diag(ErrorDomain::Process, MessageId{"process.test_spawn_failed"}).build();
    }
    SECTION("environment") {
        f.env_error = make_diag(ErrorDomain::Process, MessageId{"process.env_invalid_name"}).build();
    }
    Result<DescribedBinary> described = f.describe();
    REQUIRE_FALSE(described.has_value());
    CHECK(described.error().id == "gameserver.describe_spawn_failed");
    REQUIRE(described.error().causes.size() == 1);
    CHECK(described.error().causes[0].id.starts_with("process."));
    CHECK(f.records.empty());
}

TEST_CASE("forget drops the entry so the next describe runs the binary", "[gameserver][binary]") {
    Fixture f;
    Result<DescribedBinary> described = f.describe();
    REQUIRE(described.has_value());
    f.binary->forget(described->sha256);
    CHECK(f.cache.get().find(described->sha256) == nullptr);
    f.binary->forget(described->sha256);
    REQUIRE(f.describe().has_value());
    CHECK(f.spawns() == 2);
}

TEST_CASE("destroying the binary kills a running describe and drops the callback", "[gameserver][binary]") {
    Fixture f;
    f.script = DescribeScript{{}, std::nullopt};
    bool called = false;
    f.binary->describe([&called](Result<DescribedBinary>) { called = true; });
    f.strand.run_until([&] { return f.spawns() == 1; });
    f.binary.reset();
    f.strand.advance(20s);
    CHECK_FALSE(called);
    CHECK(f.launcher.children()[0]->terminated());
    REQUIRE(f.records.size() == 2);
    CHECK(f.records[1].second == process::RecordChange::Exited);
}

TEST_CASE("destroying the binary while hashing drops the callback", "[gameserver][binary]") {
    Fixture f;
    bool called = false;
    f.binary->describe([&called](Result<DescribedBinary>) { called = true; });
    f.binary.reset();
    // The worker's result still arrives on the strand and is ignored.
    f.strand.advance(1s);
    std::optional<Result<void>> flushed;
    f.cache.flush({}, [&flushed](Result<void> result) { flushed = std::move(result); });
    f.strand.run_until([&] { return flushed.has_value(); });
    CHECK_FALSE(called);
    CHECK(f.spawns() == 0);
}
