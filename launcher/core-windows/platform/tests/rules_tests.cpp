#include <catch2/catch_test_macros.hpp>

#include <array>
#include <set>
#include <string>
#include <vector>

#include "https_url.hpp"
#include "messages.hpp"
#include "reboot/os_windows/platform/unsupported_peer_inspector.hpp"
#include "reboot/os_windows/platform/windows_integration_registrar.hpp"
#include "reboot/os_windows/platform/windows_prerequisites.hpp"
#include "registrar_rules.hpp"
#include "socket_rows.hpp"
#include "velopack_layout.hpp"
#include "win_error.hpp"

using namespace rb;
using namespace rb::os_windows::platform;

namespace {

[[nodiscard]] IpAddress ip(std::string_view text) { return *IpAddress::parse(text); }

[[nodiscard]] Endpoint endpoint(std::string_view address, u16 port) { return {ip(address), Port{port}}; }

}  // namespace

TEST_CASE("a listener on the exact address owns the port", "[ports]") {
    const std::vector<SocketRow> rows{{ip("127.0.0.1"), Port{7777}, 40, true}, {ip("10.0.0.2"), Port{7777}, 41, true}};
    CHECK(tcp_owner_pid(rows, endpoint("127.0.0.1", 7777)) == 40u);
    CHECK(tcp_owner_pid(rows, endpoint("10.0.0.2", 7777)) == 41u);
    CHECK_FALSE(tcp_owner_pid(rows, endpoint("127.0.0.1", 7778)));
}

TEST_CASE("wildcard listeners cover their family, and :: covers v4 too", "[ports]") {
    const std::vector<SocketRow> v4_any{{ip("0.0.0.0"), Port{3551}, 7, true}};
    CHECK(tcp_owner_pid(v4_any, endpoint("127.0.0.1", 3551)) == 7u);
    CHECK_FALSE(tcp_owner_pid(v4_any, endpoint("::1", 3551)));
    const std::vector<SocketRow> dual_stack{{ip("::"), Port{3551}, 8, true}};
    CHECK(tcp_owner_pid(dual_stack, endpoint("127.0.0.1", 3551)) == 8u);
    CHECK(tcp_owner_pid(dual_stack, endpoint("::1", 3551)) == 8u);
}

TEST_CASE("the exact listener wins over a wildcard one", "[ports]") {
    const std::vector<SocketRow> rows{{ip("0.0.0.0"), Port{80}, 4, true}, {ip("127.0.0.1"), Port{80}, 99, true}};
    CHECK(tcp_owner_pid(rows, endpoint("127.0.0.1", 80)) == 99u);
}

TEST_CASE("TIME_WAIT rows have no owner, connected sockets count only exactly", "[ports]") {
    const std::vector<SocketRow> time_wait{{ip("127.0.0.1"), Port{5000}, 0, false}};
    CHECK_FALSE(tcp_owner_pid(time_wait, endpoint("127.0.0.1", 5000)));
    const std::vector<SocketRow> connected{{ip("127.0.0.1"), Port{5000}, 12, false}};
    CHECK(tcp_owner_pid(connected, endpoint("127.0.0.1", 5000)) == 12u);
    CHECK_FALSE(tcp_owner_pid(connected, endpoint("10.1.1.1", 5000)));
}

TEST_CASE("a UDP port is owned by whoever binds it on any address", "[ports]") {
    const std::vector<SocketRow> rows{{ip("0.0.0.0"), Port{7777}, 0, false}, {ip("::"), Port{7777}, 31, false}};
    CHECK(udp_owner_pid(rows, Port{7777}) == 31u);
    CHECK_FALSE(udp_owner_pid(rows, Port{7778}));
}

TEST_CASE("the registered program is the quoted or first token", "[registrar]") {
    // Outside the macros: MSVC mangles raw strings it stringizes.
    const std::string quoted = R"("C:\Users\a b\Reboot\current\Reboot.exe" --activate-url "%1")";
    const std::string bare = R"(C:\Reboot\reboot-engine.exe run --origin=service-manager)";
    const std::string unbalanced = R"("C:\unbalanced.exe --activate-url)";
    CHECK(command_program(quoted) == NativePath(L"C:\\Users\\a b\\Reboot\\current\\Reboot.exe"));
    CHECK(command_program(bare) == NativePath(L"C:\\Reboot\\reboot-engine.exe"));
    CHECK(command_program("  \"C:\\x.exe\"") == NativePath(L"C:\\x.exe"));
    CHECK_FALSE(command_program(unbalanced));
    CHECK_FALSE(command_program(""));
    CHECK_FALSE(command_program("\"\" x"));
}

TEST_CASE("entry commands quote the program", "[registrar]") {
    const std::string scheme = R"("C:\Program Files\Reboot\Reboot.exe" --activate-url "%1")";
    const std::string alone = R"("C:\e.exe")";
    CHECK(entry_command(NativePath(L"C:\\Program Files\\Reboot\\Reboot.exe"), "--activate-url \"%1\"") == scheme);
    CHECK(entry_command(NativePath(L"C:\\e.exe"), "") == alone);
}

TEST_CASE("StartupApproved marks a turned-off entry with an odd first byte", "[registrar]") {
    CHECK_FALSE(startup_disabled(std::array<u8, 12>{0x02}));
    CHECK_FALSE(startup_disabled(std::array<u8, 12>{0x06}));
    CHECK(startup_disabled(std::array<u8, 12>{0x03}));
    CHECK(startup_disabled(std::array<u8, 12>{0x07}));
    CHECK_FALSE(startup_disabled(std::span<const u8>{}));
}

TEST_CASE("ownership follows the program's place and existence", "[registrar]") {
    const NativePath root(L"C:\\Users\\a\\AppData\\Local\\RebootLauncher");
    CHECK(ownership(root / "current" / "Reboot.exe", true, root) == ports::IntegrationState::Ours);
    CHECK(ownership(NativePath(L"C:\\USERS\\A\\appdata\\local\\rebootlauncher\\current\\Reboot.exe"), true, root) ==
          ports::IntegrationState::Ours);
    CHECK(ownership(NativePath(L"C:\\Other\\app.exe"), true, root) == ports::IntegrationState::Foreign);
    CHECK(ownership(root / "current" / "Reboot.exe", false, root) == ports::IntegrationState::Stale);
}

TEST_CASE("the engine task name carries the SID", "[registrar]") {
    // core-windows/ipc's WindowsEngineStarter looks the task up by this exact name in the root folder.
    CHECK(engine_task_name("S-1-5-21-1-2-3-1001") == "Reboot Launcher Engine S-1-5-21-1-2-3-1001");
}

TEST_CASE("only https URLs with a host open", "[shell]") {
    CHECK(is_https_url("https://reboot.dev/download"));
    CHECK(is_https_url("HTTPS://Reboot.dev"));
    CHECK(is_https_url("https://user@host:443/path?q#f"));
    CHECK_FALSE(is_https_url("http://reboot.dev"));
    CHECK_FALSE(is_https_url("file:///C:/Windows"));
    CHECK_FALSE(is_https_url("https://"));
    CHECK_FALSE(is_https_url("https:///path"));
    CHECK_FALSE(is_https_url("https://host/a b"));
    CHECK_FALSE(is_https_url("https://host/\n"));
    CHECK_FALSE(is_https_url("https://user@/x"));
    CHECK_FALSE(is_https_url("ms-settings:"));
}

TEST_CASE("a Velopack install is current beside Update.exe with sq.version", "[paths]") {
    const NativePath root(L"C:\\Users\\a\\AppData\\Local\\RebootLauncher");
    const std::set<NativePath> files{root / "Update.exe", root / "current" / "sq.version"};
    const auto exists = [&files](const NativePath& file) { return files.contains(file); };
    CHECK(velopack_root_of(root / "current", exists) == root);
    CHECK(velopack_root_of(root / "Current", [&](const NativePath& file) {
              return file == root / "Update.exe" || file == root / "Current" / "sq.version";
          }) == root);
    CHECK_FALSE(velopack_root_of(root / "bin", exists));
    CHECK_FALSE(velopack_root_of(root / "current", [&](const NativePath& file) { return file == root / "Update.exe"; }));
    CHECK_FALSE(velopack_root_of(NativePath(L"C:\\portable\\current"), exists));
}

TEST_CASE("Win32 errors map to the kinds callers branch on", "[errors]") {
    CHECK(kind_of_win32(2) == ErrorKind::NotFound);
    CHECK(kind_of_win32(3) == ErrorKind::NotFound);
    CHECK(kind_of_win32(32) == ErrorKind::Conflict);
    CHECK(kind_of_win32(5) == ErrorKind::Generic);
    CHECK(kind_of_hresult(static_cast<i32>(0x80070002u)) == ErrorKind::NotFound);
    CHECK(kind_of_hresult(static_cast<i32>(0x80004005u)) == ErrorKind::Generic);
    const Diagnostic diag = call_failed("CreateFileW", 32, NativePath(L"C:\\x"));
    CHECK(diag.is(kCallFailedOnPath));
    CHECK(diag.retryable);
    REQUIRE(diag.os_error);
    CHECK(diag.os_error->code == 32);
}

TEST_CASE("the peer inspector is not supported", "[ports]") {
    UnsupportedPeerInspector inspector;
    const auto uid = inspector.peer_uid(endpoint("127.0.0.1", 1), endpoint("127.0.0.1", 2));
    REQUIRE_FALSE(uid);
    CHECK(uid.error().is(kNotSupported));
    CHECK(uid.error().kind == ErrorKind::Unsupported);
}

TEST_CASE("the Windows floor has nothing to remediate", "[prerequisites]") {
    WindowsPrerequisites probe;
    const auto statuses = probe.check();
    REQUIRE(statuses.size() == 1);
    CHECK(statuses[0].id == WindowsPrerequisites::kMinimumBuildId);
    CHECK(statuses[0].met == !statuses[0].remediation_message_id.has_value());
    const auto known = probe.remediate(WindowsPrerequisites::kMinimumBuildId);
    REQUIRE_FALSE(known);
    CHECK(known.error().is(kNoRemediation));
    CHECK(known.error().kind == ErrorKind::Unsupported);
    const auto unknown = probe.remediate("nope");
    REQUIRE_FALSE(unknown);
    CHECK(unknown.error().kind == ErrorKind::InvalidInput);
}
