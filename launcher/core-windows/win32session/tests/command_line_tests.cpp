#include <catch2/catch_test_macros.hpp>

#include <string>

#include "wide.hpp"

using namespace rb::os_windows::win32session;

namespace {

// UTF-16LE bytes of an ASCII-or-BMP string, as the engine would hand them over.
Bytes utf16(std::wstring_view text) {
    Bytes out;
    out.reserve(text.size() * 2);
    for (wchar_t c : text) {
        out.push_back(static_cast<rb::u8>(c & 0xFF));
        out.push_back(static_cast<rb::u8>((c >> 8) & 0xFF));
    }
    return out;
}

}  // namespace

TEST_CASE("to_wide drops a trailing terminator") {
    CHECK(to_wide(utf16(L"abc")) == L"abc");
    CHECK(to_wide(utf16(L"abc\0")) == L"abc");
    CHECK(to_wide(Bytes{}).empty());
}

TEST_CASE("command line leaves bare arguments unquoted") {
    const std::wstring line = build_command_line(utf16(L"game.exe"), {utf16(L"-log"), utf16(L"-nullrhi")});
    CHECK(line == L"game.exe -log -nullrhi");
}

TEST_CASE("command line quotes arguments with spaces") {
    const std::wstring line =
        build_command_line(utf16(L"C:\\Program Files\\game.exe"), {utf16(L"-epicapp=Fortnite")});
    CHECK(line == L"\"C:\\Program Files\\game.exe\" -epicapp=Fortnite");
}

TEST_CASE("command line escapes embedded quotes and the backslashes before them") {
    // a"b -> "a\"b" ; trailing backslashes before the closing quote double.
    CHECK(build_command_line(utf16(L"x"), {utf16(L"a\"b")}) == L"x \"a\\\"b\"");
    CHECK(build_command_line(utf16(L"x"), {utf16(L"a\\")}) == L"x a\\");
    CHECK(build_command_line(utf16(L"x"), {utf16(L"a b\\")}) == L"x \"a b\\\\\"");
}
