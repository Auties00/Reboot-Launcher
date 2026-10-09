#include "win32.hpp"  // first: sets _WIN32_WINNT before any std header pulls in <windows.h>

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <string>

#include "loopback_engine.hpp"
#include "reboot/os_windows/winhost/winhost_bootstrap.hpp"

using namespace rb;
using namespace rb::os_windows::winhost;
using rb::os_windows::winhost::test::base64url;

namespace {

std::array<u8, kControlTokenSize> sample_token() {
    std::array<u8, kControlTokenSize> bytes{};
    for (std::size_t i = 0; i < bytes.size(); ++i) bytes[i] = static_cast<u8>(0xF0 - i * 5);
    return bytes;
}

bool variable_set(const wchar_t* name) {
    std::array<wchar_t, 8> value{};
    return GetEnvironmentVariableW(name, value.data(), static_cast<DWORD>(value.size())) != 0 ||
           GetLastError() != ERROR_ENVVAR_NOT_FOUND;
}

void set_variable(const wchar_t* name, const std::string& value) {
    SetEnvironmentVariableW(name, std::wstring(value.begin(), value.end()).c_str());
}

}  // namespace

TEST_CASE("only a loopback tcp endpoint with a port in range is accepted") {
    CHECK(parse_ctl_endpoint("tcp://127.0.0.1:8080") == u16{8080});
    CHECK(parse_ctl_endpoint("tcp://127.0.0.1:1") == u16{1});
    CHECK(parse_ctl_endpoint("tcp://127.0.0.1:65535") == u16{65535});

    CHECK_FALSE(parse_ctl_endpoint("tcp://127.0.0.1:0"));
    CHECK_FALSE(parse_ctl_endpoint("tcp://127.0.0.1:65536"));
    CHECK_FALSE(parse_ctl_endpoint("tcp://127.0.0.1:"));
    CHECK_FALSE(parse_ctl_endpoint("tcp://127.0.0.1:+80"));
    CHECK_FALSE(parse_ctl_endpoint("tcp://127.0.0.1:-80"));
    CHECK_FALSE(parse_ctl_endpoint("tcp://127.0.0.1:80x"));
    CHECK_FALSE(parse_ctl_endpoint("tcp://127.0.0.1:80 "));
    CHECK_FALSE(parse_ctl_endpoint("tcp://127.0.0.1:4294967377"));
    CHECK_FALSE(parse_ctl_endpoint("tcp://127.0.0.2:80"));
    CHECK_FALSE(parse_ctl_endpoint("tcp://localhost:80"));
    CHECK_FALSE(parse_ctl_endpoint("tcp://10.0.0.1:80"));
    CHECK_FALSE(parse_ctl_endpoint("udp://127.0.0.1:80"));
    CHECK_FALSE(parse_ctl_endpoint(""));
}

TEST_CASE("a 43-character base64url token decodes to its 32 bytes") {
    const auto bytes = sample_token();
    const std::string text = base64url(bytes);
    REQUIRE(text.size() == 43);
    const auto token = decode_control_token(text);
    REQUIRE(token);
    CHECK(token->reveal() == bytes);
}

TEST_CASE("a token of the wrong length, alphabet or padding is rejected") {
    const std::string text = base64url(sample_token());
    CHECK_FALSE(decode_control_token(text.substr(0, 42)));
    CHECK_FALSE(decode_control_token(text + "A"));
    CHECK_FALSE(decode_control_token(text.substr(0, 42) + "="));

    std::string standard_alphabet = text;
    standard_alphabet[3] = '+';
    CHECK_FALSE(decode_control_token(standard_alphabet));
    standard_alphabet[3] = '/';
    CHECK_FALSE(decode_control_token(standard_alphabet));
}

TEST_CASE("a token whose spare trailing bits are set is not canonical and is rejected") {
    std::string text = base64url(sample_token());
    // The last character carries 4 data bits and 2 spare bits; 'B' sets the lowest spare bit.
    text.back() = static_cast<char>(text.back() == 'A' ? 'B' : text.back() + 1);
    CHECK_FALSE(decode_control_token(text));
}

TEST_CASE("read_bootstrap takes both variables out of the environment") {
    set_variable(L"REBOOT_CTL", "tcp://127.0.0.1:4100");
    set_variable(L"REBOOT_CTL_TOKEN", base64url(sample_token()));

    const auto bootstrap = read_bootstrap();
    REQUIRE(bootstrap);
    CHECK(bootstrap->port == 4100);
    CHECK(bootstrap->token.reveal() == sample_token());
    CHECK_FALSE(variable_set(L"REBOOT_CTL"));
    CHECK_FALSE(variable_set(L"REBOOT_CTL_TOKEN"));
}

TEST_CASE("read_bootstrap fails at the bootstrap step and still removes what it found") {
    set_variable(L"REBOOT_CTL", "tcp://127.0.0.1:4100");
    SetEnvironmentVariableW(L"REBOOT_CTL_TOKEN", nullptr);
    auto missing = read_bootstrap();
    REQUIRE_FALSE(missing);
    CHECK(missing.error().step == FailureStep::Bootstrap);
    CHECK_FALSE(variable_set(L"REBOOT_CTL"));

    set_variable(L"REBOOT_CTL", "tcp://192.168.1.2:4100");
    set_variable(L"REBOOT_CTL_TOKEN", base64url(sample_token()));
    auto off_host = read_bootstrap();
    REQUIRE_FALSE(off_host);
    CHECK(off_host.error().step == FailureStep::Bootstrap);
    CHECK_FALSE(variable_set(L"REBOOT_CTL_TOKEN"));
}
