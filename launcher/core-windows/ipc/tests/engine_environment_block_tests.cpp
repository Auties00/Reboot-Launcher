#include <catch2/catch_test_macros.hpp>

#include <string>
#include <vector>

#include "engine_environment_block.hpp"

using namespace std::string_literals;
using rb::DataRoot;
using rb::NativePath;
using rb::os_windows::ipc::engine_environment_block;

TEST_CASE("every REBOOT_ variable is dropped whatever its case", "[engine_environment_block]") {
    const std::vector<std::wstring> inherited{L"PATH=C:\\bin", L"reboot_launcher_home=C:\\old", L"REBOOT_DEBUG=1",
                                              L"REBOOTX=1", L"=C:=C:\\dir"};
    const DataRoot root{NativePath{L"C:\\root"}, false};
    CHECK(engine_environment_block(inherited, root) == L"PATH=C:\\bin\0REBOOTX=1\0=C:=C:\\dir\0\0"s);
}

TEST_CASE("an overridden root replaces any inherited REBOOT_LAUNCHER_HOME", "[engine_environment_block]") {
    const std::vector<std::wstring> inherited{L"REBOOT_LAUNCHER_HOME=C:\\old", L"TEMP=C:\\tmp"};
    const DataRoot root{NativePath{L"D:\\games\\reboot"}, true};
    CHECK(engine_environment_block(inherited, root) == L"REBOOT_LAUNCHER_HOME=D:\\games\\reboot\0TEMP=C:\\tmp\0\0"s);
}

TEST_CASE("REBOOT_LAUNCHER_HOME keeps the block sorted by name, ignoring case", "[engine_environment_block]") {
    const DataRoot root{NativePath{L"D:\\r"}, true};
    const std::vector<std::wstring> around{L"=C:=C:\\dir", L"Path=C:\\bin", L"REBOOT_DEBUG=1", L"systemroot=C:\\Windows",
                                           L"windir=C:\\Windows"};
    CHECK(engine_environment_block(around, root) ==
          L"=C:=C:\\dir\0Path=C:\\bin\0REBOOT_LAUNCHER_HOME=D:\\r\0systemroot=C:\\Windows\0windir=C:\\Windows\0\0"s);
    const std::vector<std::wstring> before{L"ALLUSERSPROFILE=C:\\ProgramData"};
    CHECK(engine_environment_block(before, root) == L"ALLUSERSPROFILE=C:\\ProgramData\0REBOOT_LAUNCHER_HOME=D:\\r\0\0"s);
}

TEST_CASE("an empty environment is still a terminated block", "[engine_environment_block]") {
    const DataRoot root{NativePath{L"C:\\root"}, false};
    CHECK(engine_environment_block({}, root) == L"\0\0"s);
}
