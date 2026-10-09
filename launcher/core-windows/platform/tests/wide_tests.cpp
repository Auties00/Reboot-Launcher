#include "win32.hpp"  // first: sets _WIN32_WINNT before any std header pulls in <windows.h>

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <string>
#include <vector>

#include "wide.hpp"

using namespace reboot;
using namespace reboot::os_windows::platform;
using namespace std::chrono_literals;

namespace {

// What CommandLineToArgvW, and so the CRT, makes of a command line.
[[nodiscard]] std::vector<std::wstring> parse(const std::wstring& line) {
    int count = 0;
    wchar_t** argv = CommandLineToArgvW(line.c_str(), &count);
    std::vector<std::wstring> args(argv, argv + count);
    LocalFree(argv);
    return args;
}

}  // namespace

TEST_CASE("plain arguments stay unquoted", "[wide]") {
    const std::vector<std::string> args{"-log", "-epicapp=Fortnite"};
    CHECK(build_command_line(NativePath(L"C:\\game\\game.exe"), args) == L"C:\\game\\game.exe -log -epicapp=Fortnite");
}

TEST_CASE("arguments round trip through CommandLineToArgvW", "[wide]") {
    const std::vector<std::string> args{"",          "two words", "quote\"inside", "trailing\\",
                                        "a\\\\\"b",  "tab\there", "\\\\server\\share\\", "-AUTH_PASSWORD=p a\"s\\"};
    const std::wstring line = build_command_line(NativePath(L"C:\\Program Files\\Reboot\\game.exe"), args);
    const std::vector<std::wstring> parsed = parse(line);
    REQUIRE(parsed.size() == args.size() + 1);
    CHECK(parsed[0] == L"C:\\Program Files\\Reboot\\game.exe");
    for (std::size_t i = 0; i < args.size(); ++i) CHECK(parsed[i + 1] == widen(args[i]));
}

TEST_CASE("an empty argument stays an empty quoted argument", "[wide]") {
    std::wstring line;
    append_argument(line, L"");
    CHECK(line == L"\"\"");
}

TEST_CASE("extended paths lift MAX_PATH without breaking roots", "[wide]") {
    CHECK(extended_path(NativePath(L"C:\\data\\file.json")) == L"\\\\?\\C:\\data\\file.json");
    CHECK(extended_path(NativePath(L"C:\\data\\..\\other\\")) == L"\\\\?\\C:\\other");
    CHECK(extended_path(NativePath(L"C:/mixed/slashes")) == L"\\\\?\\C:\\mixed\\slashes");
    CHECK(extended_path(NativePath(L"C:\\")) == L"\\\\?\\C:\\");
    CHECK(extended_path(NativePath(L"\\\\server\\share\\dir")) == L"\\\\?\\UNC\\server\\share\\dir");
    CHECK(extended_path(NativePath(L"\\\\?\\C:\\already")) == L"\\\\?\\C:\\already");
    const std::wstring relative = extended_path(NativePath(L"relative\\file"));
    CHECK(relative.starts_with(L"\\\\?\\"));
    CHECK(relative.ends_with(L"\\relative\\file"));
}

TEST_CASE("shell paths drop the extended prefix and trailing separators", "[wide]") {
    CHECK(shell_path(NativePath(L"\\\\?\\C:\\dir\\")) == L"C:\\dir");
    CHECK(shell_path(NativePath(L"C:\\")) == L"C:\\");
    CHECK(shell_path(NativePath(L"C:/a/./b")) == L"C:\\a\\b");
}

TEST_CASE("the environment overlay replaces names case-insensitively", "[wide]") {
    const std::vector<std::wstring> base{L"Path=C:\\Windows", L"SystemRoot=C:\\Windows", L"=C:=C:\\dir", L"TEMP=C:\\t"};
    const std::wstring block = build_environment_block(base, {{"PATH", "D:\\tools"}, {"REBOOT_CHANNEL", "x=y"}});
    std::wstring expected;
    for (const wchar_t* entry : {L"=C:=C:\\dir", L"PATH=D:\\tools", L"REBOOT_CHANNEL=x=y", L"SystemRoot=C:\\Windows", L"TEMP=C:\\t"}) {
        expected += entry;
        expected += L'\0';
    }
    expected += L'\0';
    CHECK(block == expected);
}

TEST_CASE("an empty environment block still ends in two NULs", "[wide]") {
    CHECK(build_environment_block({}, {}) == std::wstring(2, L'\0'));
}

TEST_CASE("FILETIME ticks convert both ways", "[wide]") {
    const auto epoch = from_filetime(116444736000000000ull);
    CHECK(epoch.time_since_epoch() == std::chrono::system_clock::duration::zero());
    const u64 ticks = 133700000001234567ull;
    CHECK(to_filetime(from_filetime(ticks)) == ticks);
}

TEST_CASE("creation times compare at microseconds", "[wide]") {
    const auto base = from_filetime(133700000001234560ull);
    const auto later_in_same_microsecond =
        base + std::chrono::duration_cast<std::chrono::system_clock::duration>(std::chrono::nanoseconds{900});
    CHECK(same_creation_time(base, later_in_same_microsecond));
    CHECK_FALSE(same_creation_time(base, base + 1us));
    CHECK_FALSE(same_creation_time(base, base - 1h));
}
