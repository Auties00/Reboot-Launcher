#include "win32.hpp"  // first: sets _WIN32_WINNT before any std header pulls in <windows.h>

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cstdint>

#include "handle_list.hpp"
#include "wide.hpp"

using namespace reboot::os_windows::win32session;

namespace {

HANDLE fake(std::uintptr_t value) { return reinterpret_cast<HANDLE>(value); }

Bytes units(std::initializer_list<char16_t> text) {
    Bytes out;
    for (char16_t c : text) {
        out.push_back(static_cast<reboot::u8>(c & 0xFF));
        out.push_back(static_cast<reboot::u8>(c >> 8));
    }
    return out;
}

}  // namespace

TEST_CASE("a companion's three NUL stdio handles collapse to one handle-list entry") {
    std::array<HANDLE, 3> handles{fake(0x40), fake(0x40), fake(0x40)};
    REQUIRE(unique_handles(handles) == 1);
    CHECK(handles[0] == fake(0x40));
}

TEST_CASE("distinct stdio handles are all kept in order") {
    std::array<HANDLE, 3> handles{fake(0x40), fake(0x44), fake(0x48)};
    REQUIRE(unique_handles(handles) == 3);
    CHECK(handles == std::array<HANDLE, 3>{fake(0x40), fake(0x44), fake(0x48)});
}

TEST_CASE("a repeated handle in the middle is dropped") {
    std::array<HANDLE, 3> handles{fake(0x40), fake(0x44), fake(0x40)};
    REQUIRE(unique_handles(handles) == 2);
    CHECK(handles[0] == fake(0x40));
    CHECK(handles[1] == fake(0x44));
}

TEST_CASE("an environment block already ending in two NUL units is left alone") {
    Bytes block = units({u'A', u'=', u'1', 0, 0});
    const Bytes before = block;
    terminate_env_block(block);
    CHECK(block == before);
}

TEST_CASE("an environment block with one terminator gets the second") {
    Bytes block = units({u'A', u'=', u'1', 0});
    terminate_env_block(block);
    CHECK(block == units({u'A', u'=', u'1', 0, 0}));
}

TEST_CASE("an unterminated environment block is terminated although its last byte is zero") {
    // UTF-16LE 'A' ends in a zero high byte, which a byte-wise check mistakes for a terminator.
    Bytes block = units({u'A', u'=', u'1'});
    terminate_env_block(block);
    CHECK(block == units({u'A', u'=', u'1', 0, 0}));
}

TEST_CASE("an odd-length environment block is padded to whole units first") {
    Bytes block = units({u'A', u'=', u'1'});
    block.push_back(0);  // half a unit
    terminate_env_block(block);
    CHECK(block.size() % 2 == 0);
    CHECK(block == units({u'A', u'=', u'1', 0, 0}));
}

TEST_CASE("arguments with a newline or vertical tab are quoted") {
    CHECK(build_command_line(units({u'x'}), {units({u'a', u'\n', u'b'})}) == L"x \"a\nb\"");
    CHECK(build_command_line(units({u'x'}), {units({u'a', u'\v', u'b'})}) == L"x \"a\vb\"");
}
