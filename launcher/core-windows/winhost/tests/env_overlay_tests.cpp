#include "win32.hpp"  // first: sets _WIN32_WINNT before any std header pulls in <windows.h>

#include <catch2/catch_test_macros.hpp>

#include <string>
#include <vector>

#include "env_overlay.hpp"

using namespace reboot;
using namespace reboot::os_windows::winhost;
using contracts::winhost::Bytes;

namespace {

Bytes utf16(std::wstring_view text) {
    Bytes out;
    for (const wchar_t unit : text) {
        out.push_back(static_cast<u8>(unit & 0xFF));
        out.push_back(static_cast<u8>((unit >> 8) & 0xFF));
    }
    return out;
}

// The NUL-separated entries of a block, checking it ends in exactly one empty entry.
std::vector<std::wstring> entries_of(const Bytes& block) {
    REQUIRE(block.size() % 2 == 0);
    std::wstring units;
    for (std::size_t i = 0; i < block.size(); i += 2) units.push_back(static_cast<wchar_t>(block[i] | (block[i + 1] << 8)));
    REQUIRE(units.size() >= 2);
    CHECK(units.substr(units.size() - 2) == std::wstring(2, L'\0'));
    std::vector<std::wstring> out;
    std::size_t start = 0;
    while (start < units.size() - 1) {
        const std::size_t end = units.find(L'\0', start);
        out.push_back(units.substr(start, end - start));
        start = end + 1;
    }
    return out;
}

std::wstring block_of(std::initializer_list<std::wstring_view> entries) {
    std::wstring out;
    for (const auto entry : entries) {
        out += entry;
        out += L'\0';
    }
    out += L'\0';
    return out;
}

}  // namespace

TEST_CASE("overlay variables replace base ones by case-insensitive name and the block is sorted") {
    const std::wstring base = block_of({L"=C:=C:\\work", L"Path=C:\\bin", L"ZED=1", L"SystemRoot=C:\\windows"});
    const Bytes overlay = utf16(block_of({L"PATH=D:\\game", L"REBOOT_CTL=tcp://127.0.0.1:9"}));

    CHECK(entries_of(overlay_environment(base.c_str(), overlay)) ==
          std::vector<std::wstring>{L"=C:=C:\\work", L"PATH=D:\\game", L"REBOOT_CTL=tcp://127.0.0.1:9",
                                    L"SystemRoot=C:\\windows", L"ZED=1"});
}

TEST_CASE("an empty overlay leaves the base environment, sorted and terminated") {
    const std::wstring base = block_of({L"b=2", L"A=1"});
    CHECK(entries_of(overlay_environment(base.c_str(), {})) == std::vector<std::wstring>{L"A=1", L"b=2"});
}

TEST_CASE("an overlay without its terminator is still read to its end") {
    const std::wstring base = block_of({L"A=1"});
    const Bytes overlay = utf16(L"B=2");
    CHECK(entries_of(overlay_environment(base.c_str(), overlay)) == std::vector<std::wstring>{L"A=1", L"B=2"});
}

TEST_CASE("a later overlay entry for the same name wins") {
    const std::wstring base = block_of({L"A=1"});
    const Bytes overlay = utf16(block_of({L"X=first", L"x=second"}));
    CHECK(entries_of(overlay_environment(base.c_str(), overlay)) == std::vector<std::wstring>{L"A=1", L"x=second"});
}

TEST_CASE("a value holding '=' keeps its name up to the first one") {
    const std::wstring base = block_of({L"OPTS=a=b"});
    const Bytes overlay = utf16(block_of({L"OPTS=c=d"}));
    CHECK(entries_of(overlay_environment(base.c_str(), overlay)) == std::vector<std::wstring>{L"OPTS=c=d"});
}
