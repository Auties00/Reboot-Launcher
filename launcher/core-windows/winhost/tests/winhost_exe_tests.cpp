#include "loopback_engine.hpp"  // first: pulls in win32.hpp before any std header

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <string>

#include "reboot/contracts/common.hpp"
#include "reboot/contracts/winhost.hpp"
#include "reboot/foundation/version.hpp"
#include "reboot/os_windows/winhost/winhost_exit.hpp"

using namespace rb;
using namespace rb::os_windows::winhost;
using rb::os_windows::winhost::test::base64url;
using rb::os_windows::winhost::test::LoopbackEngine;
using rb::os_windows::winhost::test::system_path;
using rb::os_windows::winhost::test::utf16;
namespace wh = rb::contracts::winhost;
namespace common = rb::contracts::common;

namespace {

// reboot-winhost.exe started with the given REBOOT_CTL and REBOOT_CTL_TOKEN; killed if a check
// fails before it exits.
class WinhostProcess {
public:
    WinhostProcess(const std::string& ctl, const std::string& token) {
        std::wstring env;
        if (!ctl.empty()) env += L"REBOOT_CTL=" + std::wstring(ctl.begin(), ctl.end()) + L'\0';
        if (!token.empty()) env += L"REBOOT_CTL_TOKEN=" + std::wstring(token.begin(), token.end()) + L'\0';
        std::array<wchar_t, MAX_PATH> root{};
        const DWORD root_length = GetEnvironmentVariableW(L"SystemRoot", root.data(), static_cast<DWORD>(root.size()));
        env += L"SystemRoot=" + std::wstring(root.data(), root_length) + L'\0';
        env += L'\0';

        const std::string exe_narrow = REBOOT_WINHOST_EXE;
        const std::wstring exe(exe_narrow.begin(), exe_narrow.end());
        std::wstring command_line = L"\"" + exe + L"\"";
        STARTUPINFOW startup{};
        startup.cb = sizeof(startup);
        PROCESS_INFORMATION info{};
        if (CreateProcessW(exe.c_str(), command_line.data(), nullptr, nullptr, FALSE,
                           CREATE_UNICODE_ENVIRONMENT | CREATE_NO_WINDOW, env.data(), nullptr, &startup, &info) != 0) {
            CloseHandle(info.hThread);
            process_ = info.hProcess;
        }
    }

    ~WinhostProcess() {
        if (process_ == nullptr) return;
        if (WaitForSingleObject(process_, 0) != WAIT_OBJECT_0) TerminateProcess(process_, 99);
        CloseHandle(process_);
    }

    WinhostProcess(const WinhostProcess&) = delete;
    WinhostProcess& operator=(const WinhostProcess&) = delete;

    [[nodiscard]] bool started() const noexcept { return process_ != nullptr; }

    [[nodiscard]] std::optional<DWORD> exit_code_within(DWORD ms) const {
        if (WaitForSingleObject(process_, ms) != WAIT_OBJECT_0) return std::nullopt;
        DWORD code = 0;
        GetExitCodeProcess(process_, &code);
        return code;
    }

private:
    HANDLE process_ = nullptr;
};

constexpr DWORD code_of(WinhostExit exit) { return static_cast<DWORD>(exit); }

}  // namespace

TEST_CASE("reboot-winhost.exe without its bootstrap variables exits with BadBootstrap") {
    const WinhostProcess winhost("", "");
    REQUIRE(winhost.started());
    CHECK(winhost.exit_code_within(15000) == code_of(WinhostExit::BadBootstrap));
}

TEST_CASE("reboot-winhost.exe with no engine listening exits with ConnectFailed") {
    u16 port = 0;
    {
        LoopbackEngine closed;
        port = closed.port();
    }
    std::array<u8, 32> token{};
    const WinhostProcess winhost("tcp://127.0.0.1:" + std::to_string(port), base64url(token));
    REQUIRE(winhost.started());
    CHECK(winhost.exit_code_within(15000) == code_of(WinhostExit::ConnectFailed));
}

TEST_CASE("killing the engine's end of the channel ends reboot-winhost.exe and its game") {
    LoopbackEngine engine;
    std::array<u8, 32> token{};
    for (std::size_t i = 0; i < token.size(); ++i) token[i] = static_cast<u8>(i + 1);
    const WinhostProcess winhost("tcp://127.0.0.1:" + std::to_string(engine.port()), base64url(token));
    REQUIRE(winhost.started());

    engine.accept_peer();
    const auto preamble = engine.read_preamble();
    REQUIRE(preamble);
    CHECK(*preamble == game_control_preamble(VersionStreams::payload_abi));
    const auto hello = engine.expect<wh::WhHello>();
    REQUIRE(hello);
    CHECK(hello->token == token);
    CHECK(hello->protocol == wh::kWinhostProtocol);

    wh::SpawnGame spawn;
    spawn.exe_utf16 = utf16(system_path(L"PING.EXE"));
    for (const auto arg : {L"-n", L"60", L"127.0.0.1"}) spawn.argv_utf16.push_back(utf16(arg));
    spawn.inject_timeout_ms = 5000;
    spawn.drain_timeout_ms = 5000;
    engine.send(wh::WhWelcome{std::move(spawn)});
    const auto spawned = engine.expect<wh::Spawned>();
    REQUIRE(spawned);
    const HANDLE game = OpenProcess(SYNCHRONIZE, FALSE, spawned->pid);
    REQUIRE(game != nullptr);
    engine.send(wh::Resume{1});
    const auto resumed = engine.expect<common::CommandResult>();
    REQUIRE(resumed);
    CHECK(resumed->ok);

    engine.close_peer();
    CHECK(winhost.exit_code_within(15000) == code_of(WinhostExit::EngineClosed));
    CHECK(WaitForSingleObject(game, 5000) == WAIT_OBJECT_0);
    CloseHandle(game);
}
