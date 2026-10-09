#include <catch2/catch_test_macros.hpp>

#include <string>
#include <variant>
#include <vector>

#include "messages.hpp"
#include "reboot/foundation/sha256.hpp"
#include "reboot/os_windows/platform/win32_session_host.hpp"
#include "reboot/testing/fake_platform_paths.hpp"
#include "reboot/testing/in_memory_file_system.hpp"
#include "session_mapping.hpp"

using namespace reboot;
using namespace reboot::os_windows::platform;
namespace wh = reboot::contracts::winhost;
namespace w32 = reboot::os_windows::win32session;

namespace {

[[nodiscard]] std::vector<u8> bytes(std::string_view text) { return {text.begin(), text.end()}; }

struct HostFixture {
    testing::InMemoryFileSystem fs;
    NativePath dll = testing::default_fake_root() / "payload" / "client.dll";
    std::vector<ports::SessionHostEvent> events;

    [[nodiscard]] ports::SessionLaunch launch_with(std::array<u8, 32> sha256) const {
        ports::SessionLaunch launch;
        launch.exe = testing::default_fake_root() / "game" / "FortniteClient-Win64-Shipping.exe";
        launch.inject.push_back({dll, sha256, ports::BootStrategy::EarlyBirdApc, ports::InjectPhase::Early});
        return launch;
    }
};

}  // namespace

TEST_CASE("win32session events map onto the port's vocabulary", "[session]") {
    const auto spawned = to_port_event(wh::Spawned{wh::ProcessRole::Companion, 42});
    REQUIRE(std::holds_alternative<ports::Spawned>(spawned));
    CHECK(std::get<ports::Spawned>(spawned).role == ports::SessionRole::Companion);
    CHECK(std::get<ports::Spawned>(spawned).pid == 42);

    const auto injected = to_port_event(wh::Injected{utf16_bytes(L"C:\\p\\a.dll"), false, 577});
    REQUIRE(std::holds_alternative<ports::Injected>(injected));
    const auto& result = std::get<ports::Injected>(injected);
    CHECK(result.path == NativePath(L"C:\\p\\a.dll"));
    CHECK_FALSE(result.ok);
    REQUIRE(result.error);
    CHECK(result.error->origin == SystemError::Origin::Host);
    CHECK(result.error->code == 577);

    const auto output = to_port_event(wh::Output{wh::ProcessRole::Game, wh::OutputStream::Stderr, bytes("line\n")});
    REQUIRE(std::holds_alternative<ports::Output>(output));
    CHECK(std::get<ports::Output>(output).role == ports::SessionRole::Game);
    CHECK(std::get<ports::Output>(output).stream == wh::OutputStream::Stderr);
    CHECK(std::get<ports::Output>(output).bytes == bytes("line\n"));

    const auto exited = to_port_event(wh::Exited{wh::ProcessRole::Game, 3});
    REQUIRE(std::holds_alternative<ports::Exited>(exited));
    CHECK(std::get<ports::Exited>(exited).code == 3);
    CHECK_FALSE(std::get<ports::Exited>(to_port_event(wh::Exited{wh::ProcessRole::Game, std::nullopt})).code);
}

TEST_CASE("UTF-16 paths survive the winhost byte form", "[session]") {
    const NativePath path(L"C:\\Users\\Jos\u00e9\\payload\\client.dll");
    CHECK(path_of(utf16_bytes(path.native())) == path);
    wh::Bytes terminated = utf16_bytes(L"C:\\a");
    terminated.push_back(0);
    terminated.push_back(0);
    CHECK(path_of(terminated) == NativePath(L"C:\\a"));
}

TEST_CASE("launch failures name the call that failed", "[session]") {
    const NativePath exe(L"C:\\game\\game.exe");
    const Diagnostic missing = spawn_diagnostic({w32::SpawnStep::CreateGame, {SystemError::Origin::Host, 2}}, exe);
    CHECK(missing.is(kCallFailedOnPath));
    CHECK(missing.kind == ErrorKind::NotFound);
    const Diagnostic resume = spawn_diagnostic({w32::SpawnStep::Resume, {SystemError::Origin::Host, 5}}, exe);
    CHECK(resume.is(kCallFailed));
    REQUIRE(resume.os_error);
    CHECK(resume.os_error->code == 5);
}

TEST_CASE("an unopenable or altered DLL gets the payload messages", "[session]") {
    const NativePath dll(L"C:\\payload\\client.dll");
    const Diagnostic vanished = inject_diagnostic({w32::InjectStep::OpenFile, {SystemError::Origin::Host, 2}}, dll);
    CHECK(vanished.is(kFileVanished));
    CHECK(vanished.kind == ErrorKind::NotFound);
    const Diagnostic altered = inject_diagnostic({w32::InjectStep::Integrity, {SystemError::Origin::Host, 577}}, dll);
    CHECK(altered.is(kPayloadHashMismatch));
    const Diagnostic remote = inject_diagnostic({w32::InjectStep::RemoteLoad, {SystemError::Origin::Host, 126}}, dll);
    CHECK(remote.is(kCallFailedOnPath));
}

TEST_CASE("a payload that vanished before launch fails without spawning", "[session]") {
    HostFixture fixture;
    Win32SessionHost host(fixture.fs);
    auto session = host.launch(fixture.launch_with({}), [&](ports::SessionHostEvent event) { fixture.events.push_back(event); });
    REQUIRE_FALSE(session);
    CHECK(session.error().is(kFileVanished));
    CHECK(session.error().kind == ErrorKind::NotFound);
    CHECK(fixture.events.empty());
}

TEST_CASE("a payload whose hash moved fails without spawning", "[session]") {
    HostFixture fixture;
    fixture.fs.make_dir(fixture.dll.parent_path());
    fixture.fs.write(fixture.dll, bytes("tampered"));
    Win32SessionHost host(fixture.fs);
    auto session = host.launch(fixture.launch_with(sha256(bytes("original"))),
                               [&](ports::SessionHostEvent event) { fixture.events.push_back(event); });
    REQUIRE_FALSE(session);
    CHECK(session.error().is(kPayloadHashMismatch));
    CHECK(fixture.events.empty());
}

TEST_CASE("a verified payload lets the launch proceed to the spawn", "[session]") {
    HostFixture fixture;
    fixture.fs.make_dir(fixture.dll.parent_path());
    fixture.fs.write(fixture.dll, bytes("original"));
    Win32SessionHost host(fixture.fs);
    // The fake root's game does not exist, so the spawn itself fails after the payload passed.
    auto session = host.launch(fixture.launch_with(sha256(bytes("original"))),
                               [&](ports::SessionHostEvent event) { fixture.events.push_back(event); });
    REQUIRE_FALSE(session);
    CHECK(session.error().is(kCallFailedOnPath));
    const Arg* call = session.error().find_arg("call");
    REQUIRE(call != nullptr);
    CHECK(std::get<std::string>(*call) == "CreateProcessW");
}
