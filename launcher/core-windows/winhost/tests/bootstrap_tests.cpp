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

// This process's stdin, for its lifetime: a pipe holding `text`, closed for writing after it.
class StdinFeed {
public:
    explicit StdinFeed(const std::string& text) : previous_(GetStdHandle(STD_INPUT_HANDLE)) {
        HANDLE write = nullptr;
        REQUIRE(CreatePipe(&read_, &write, nullptr, 0) != 0);
        DWORD written = 0;
        if (!text.empty())
            REQUIRE(WriteFile(write, text.data(), static_cast<DWORD>(text.size()), &written, nullptr) != 0);
        CloseHandle(write);
        SetStdHandle(STD_INPUT_HANDLE, read_);
    }
    ~StdinFeed() {
        SetStdHandle(STD_INPUT_HANDLE, previous_);
        CloseHandle(read_);
    }
    StdinFeed(const StdinFeed&) = delete;
    StdinFeed& operator=(const StdinFeed&) = delete;

    // What is left in the pipe.
    [[nodiscard]] std::string rest() const {
        std::string out;
        std::array<char, 64> buffer{};
        DWORD read = 0;
        while (ReadFile(read_, buffer.data(), static_cast<DWORD>(buffer.size()), &read, nullptr) != 0 && read != 0)
            out.append(buffer.data(), read);
        return out;
    }

private:
    HANDLE previous_;
    HANDLE read_ = nullptr;
};

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

TEST_CASE("read_bootstrap takes REBOOT_CTL out of the environment and the token line from stdin") {
    set_variable(L"REBOOT_CTL", "tcp://127.0.0.1:4100");
    const StdinFeed stdin_feed(base64url(sample_token()) + "\nnext");

    const auto bootstrap = read_bootstrap();
    REQUIRE(bootstrap);
    CHECK(bootstrap->port == 4100);
    CHECK(bootstrap->token.reveal() == sample_token());
    CHECK_FALSE(variable_set(L"REBOOT_CTL"));
    CHECK(stdin_feed.rest() == "next");
}

TEST_CASE("read_bootstrap fails at the bootstrap step and still removes what it found") {
    {
        set_variable(L"REBOOT_CTL", "tcp://127.0.0.1:4100");
        const StdinFeed empty("");
        auto missing = read_bootstrap();
        REQUIRE_FALSE(missing);
        CHECK(missing.error().step == FailureStep::Bootstrap);
        CHECK_FALSE(variable_set(L"REBOOT_CTL"));
    }
    {
        set_variable(L"REBOOT_CTL", "tcp://192.168.1.2:4100");
        const StdinFeed token(base64url(sample_token()) + "\n");
        auto off_host = read_bootstrap();
        REQUIRE_FALSE(off_host);
        CHECK(off_host.error().step == FailureStep::Bootstrap);
        CHECK_FALSE(variable_set(L"REBOOT_CTL"));
    }
    for (const std::string& line : {base64url(sample_token()), base64url(sample_token()) + "A\n"}) {
        set_variable(L"REBOOT_CTL", "tcp://127.0.0.1:4100");
        const StdinFeed unterminated(line);
        auto bad = read_bootstrap();
        REQUIRE_FALSE(bad);
        CHECK(bad.error().step == FailureStep::Bootstrap);
    }
}
