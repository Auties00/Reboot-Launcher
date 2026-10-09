#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

#include "base64.hpp"
#include "file_uri.hpp"
#include "os_release.hpp"
#include "process_environment.hpp"
#include "update_entries.hpp"

using namespace std::string_view_literals;
using reboot::u8;
using namespace reboot::os_linux::platform;

TEST_CASE("base64 round trips and refuses non-canonical text", "[base64]") {
    const std::vector<u8> bytes{0x00, 0xFF, 0x10, 0x80, 0x7F};
    for (std::size_t length = 0; length <= bytes.size(); ++length) {
        const std::vector<u8> part(bytes.begin(), bytes.begin() + static_cast<std::ptrdiff_t>(length));
        CHECK(base64_decode(base64_encode(part)) == part);
    }
    CHECK(base64_encode(std::vector<u8>{'f', 'o', 'o', 'b'}) == "Zm9vYg==");
    CHECK(base64_decode("Zm9vYg==") == std::vector<u8>{'f', 'o', 'o', 'b'});
    CHECK_FALSE(base64_decode("Zm9vYh=="));
    CHECK_FALSE(base64_decode("Zm9v=g=="));
    CHECK_FALSE(base64_decode("Zm9"));
    CHECK_FALSE(base64_decode("Zm9v!A=="));
}

TEST_CASE("file URIs percent-encode everything but unreserved bytes and slashes", "[file_uri]") {
    CHECK(file_uri("/home/ada/Reboot Launcher/a%b") == "file:///home/ada/Reboot%20Launcher/a%25b");
    CHECK(file_uri("/srv/caf\xC3\xA9") == "file:///srv/caf%C3%A9");
}

TEST_CASE("only https URLs with a host are openable", "[file_uri]") {
    CHECK(is_https_url("https://example.com/x"));
    CHECK(is_https_url("HTTPS://example.com"));
    CHECK_FALSE(is_https_url("http://example.com"));
    CHECK_FALSE(is_https_url("file:///etc/hosts"));
    CHECK_FALSE(is_https_url("https://"));
    CHECK_FALSE(is_https_url("https:///path"));
    CHECK_FALSE(is_https_url("javascript:alert(1)"));
}

TEST_CASE("os-release values unquote", "[os_release]") {
    const OsRelease release = parse_os_release(
        "# comment\nNAME=\"Fedora Linux\"\nVERSION_ID=40\nBUILD_ID='rolling \\x'\nID=fedora\n  \nPRETTY_NAME=\"a\\\"b\"\n");
    CHECK(release.name == "Fedora Linux");
    CHECK(release.version_id == "40");
    CHECK(release.build_id == "rolling \\x");
    CHECK(parse_os_release("NAME=\"Say \\\"hi\\\"\"\n").name == "Say \"hi\"");
    CHECK(parse_os_release("").name.empty());
}

TEST_CASE("an environment block is searched by exact name", "[os_release]") {
    CHECK(environ_block_has("PATH=/bin\0container=podman\0"sv, "container"));
    CHECK(environ_block_has("container=\0"sv, "container"));
    CHECK_FALSE(environ_block_has("containers=1\0"sv, "container"));
    CHECK_FALSE(environ_block_has("PATH=/bin\0"sv, "container"));
}

TEST_CASE("the last of several assignments to a name wins in an envp", "[process_environment]") {
    const std::vector<std::string> envp = envp_strings({{"A", "1"}, {"B", "2"}, {"A", "3"}});
    CHECK(envp == std::vector<std::string>{"B=2", "A=3"});
}

TEST_CASE("update entry paths are normalised or refused", "[update_entries]") {
    CHECK(safe_entry_path("./11.0.0/bin/reboot-engine") == "11.0.0/bin/reboot-engine");
    CHECK(safe_entry_path("11.0.0//lib/./x") == "11.0.0/lib/x");
    CHECK(safe_entry_path("./") == "");
    CHECK_FALSE(safe_entry_path("/etc/passwd"));
    CHECK_FALSE(safe_entry_path("11.0.0/../../x"));
    CHECK_FALSE(safe_entry_path(".."));
    CHECK(top_component("11.0.0/bin/x") == "11.0.0");
    CHECK(top_component("11.0.0") == "11.0.0");
}

TEST_CASE("symlink targets must stay inside the extraction root", "[update_entries]") {
    CHECK(link_stays_inside("11.0.0/lib/libfoo.so", "libfoo.so.1"));
    CHECK(link_stays_inside("11.0.0/bin/tool", "../lib/tool"));
    CHECK(link_stays_inside("11.0.0/x", "../11.0.0/y"));
    CHECK_FALSE(link_stays_inside("11.0.0/x", "../../outside"));
    CHECK_FALSE(link_stays_inside("11.0.0/x", "/usr/lib/libc.so"));
    CHECK_FALSE(link_stays_inside("top", ".."));
    CHECK_FALSE(link_stays_inside("11.0.0/x", ""));
}
