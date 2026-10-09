#include <catch2/catch_test_macros.hpp>

#include <string>
#include <vector>

#include "engine_launch.hpp"
#include "reboot/os_windows/ipc/windows_engine_starter.hpp"

using namespace std::string_literals;
using reboot::DataRoot;
using reboot::NativePath;
using namespace reboot::os_windows::ipc;

TEST_CASE("the engine task is named after the user", "[engine_launch]") {
    CHECK(engine_task_name("S-1-5-21-1-2-3-1001") == "Reboot Launcher Engine S-1-5-21-1-2-3-1001");
}

TEST_CASE("the engine command line quotes the exe and asks for an on-demand origin", "[engine_launch]") {
    CHECK(engine_command_line(NativePath{L"C:\\Program Files\\Reboot\\reboot-engine.exe"}) ==
          L"\"C:\\Program Files\\Reboot\\reboot-engine.exe\" run --origin=on-demand");
}

TEST_CASE("environment entries stop at the empty entry and keep hidden drive entries", "[engine_launch]") {
    const std::wstring block = L"=C:=C:\\dir\0PATH=C:\\bin\0TEMP=C:\\tmp\0\0"s;
    CHECK(environment_entries(block.c_str()) == std::vector<std::wstring>{L"=C:=C:\\dir", L"PATH=C:\\bin", L"TEMP=C:\\tmp"});
    CHECK(environment_entries(L"\0\0").empty());
    CHECK(environment_entries(nullptr).empty());
}

TEST_CASE("a task action names the engine with or without quotes, in any case", "[engine_launch]") {
    const NativePath engine{L"C:\\Users\\me\\AppData\\Local\\RebootLauncher\\current\\reboot-engine.exe"};
    CHECK(action_runs(L"C:\\Users\\me\\AppData\\Local\\RebootLauncher\\current\\reboot-engine.exe", engine));
    CHECK(action_runs(L"\"c:\\users\\ME\\appdata\\local\\rebootlauncher\\CURRENT\\Reboot-Engine.exe\"", engine));
    CHECK(action_runs(L"C:\\Users\\me\\AppData\\Local\\RebootLauncher\\old\\..\\current\\reboot-engine.exe", engine));
}

TEST_CASE("a task action running anything else is not the engine", "[engine_launch]") {
    const NativePath engine{L"C:\\Reboot\\reboot-engine.exe"};
    CHECK_FALSE(action_runs(L"C:\\Reboot\\reboot-engine.exe.bak", engine));
    CHECK_FALSE(action_runs(L"C:\\Reboot", engine));
    CHECK_FALSE(action_runs(L"C:\\Reboot\\reboot-engine.exe\\x", engine));
    CHECK_FALSE(action_runs(L"reboot-engine.exe", engine));
    CHECK_FALSE(action_runs(L"", engine));
    CHECK_FALSE(action_runs(L"\"", engine));
}

TEST_CASE("session ids are plain decimal numbers", "[engine_launch]") {
    CHECK(parse_session_id("0") == 0u);
    CHECK(parse_session_id("17") == 17u);
    CHECK_FALSE(parse_session_id(""));
    CHECK_FALSE(parse_session_id("-1"));
    CHECK_FALSE(parse_session_id("1a"));
    CHECK_FALSE(parse_session_id("99999999999"));
}

TEST_CASE("autostart is refused before anything starts for an elevated or non-interactive caller", "[engine_starter]") {
    // A root on a drive that does not exist: any file system work would fail instead.
    const DataRoot root{NativePath{L"\\\\?\\Q:\\no-such-root"}, true};
    const NativePath exe{L"\\\\?\\Q:\\no-such-root\\reboot-engine.exe"};

    WindowsEngineStarter elevated{"S-1-5-21-1", reboot::ports::CallerContext{"1", true, true, {}}};
    CHECK(elevated.ensure_started(exe, root) == reboot::ports::StartResult::ElevatedRefused);

    WindowsEngineStarter service{"S-1-5-21-1", reboot::ports::CallerContext{"0", false, false, {}}};
    CHECK(service.ensure_started(exe, root) == reboot::ports::StartResult::NoInteractiveSession);

    WindowsEngineStarter both{"S-1-5-21-1", reboot::ports::CallerContext{"0", true, false, {}}};
    CHECK(both.ensure_started(exe, root) == reboot::ports::StartResult::ElevatedRefused);
}
