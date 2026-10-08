#include <catch2/catch_test_macros.hpp>

#include <string>
#include <vector>

#include "engine_environment_block.hpp"

using namespace std::string_literals;
using reboot::DataRoot;
using reboot::NativePath;
using reboot::os_windows::ipc::engine_environment_block;

TEST_CASE("every REBOOT_ variable is dropped whatever its case", "[engine_environment_block]") {
    const std::vector<std::wstring> inherited{L"PATH=C:\\bin", L"reboot_launcher_home=C:\\old", L"REBOOT_DEBUG=1",
                                              L"REBOOTX=1", L"=C:=C:\\dir"};
    const DataRoot root{NativePath{L"C:\\root"}, false};
    CHECK(engine_environment_block(inherited, root) == L"PATH=C:\\bin\0REBOOTX=1\0=C:=C:\\dir\0\0"s);
}

TEST_CASE("an overridden root replaces any inherited REBOOT_LAUNCHER_HOME", "[engine_environment_block]") {
    const std::vector<std::wstring> inherited{L"REBOOT_LAUNCHER_HOME=C:\\old", L"TEMP=C:\\tmp"};
    const DataRoot root{NativePath{L"D:\\games\\reboot"}, true};
    CHECK(engine_environment_block(inherited, root) == L"TEMP=C:\\tmp\0REBOOT_LAUNCHER_HOME=D:\\games\\reboot\0\0"s);
}

TEST_CASE("an empty environment is still a terminated block", "[engine_environment_block]") {
    const DataRoot root{NativePath{L"C:\\root"}, false};
    CHECK(engine_environment_block({}, root) == L"\0\0"s);
}
