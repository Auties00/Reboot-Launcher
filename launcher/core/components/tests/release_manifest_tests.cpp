#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <fstream>
#include <iterator>
#include <string>
#include <string_view>

#include "components_test_support.hpp"
#include "reboot/components/payload_entry.hpp"
#include "reboot/components/release_manifest.hpp"

using namespace rb;
using namespace rb::components;
using rb::components::test::arg_text;
using rb::components::test::bytes_of;

namespace {

std::string read_data(std::string_view name) {
    std::ifstream in(std::string(REBOOT_COMPONENTS_TEST_DATA) + "/" + std::string(name), std::ios::binary);
    REQUIRE(in);
    return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

Result<ReleaseManifest> parse(std::string_view text) { return parse_release_manifest(bytes_of(text)); }

// A minimal valid manifest with `members` spliced in after the required ones.
std::string with(std::string_view members) {
    return R"({"schema":1,"serial":3,"expires_unix_ms":4000000000000)" + std::string(members.empty() ? "" : ",") +
           std::string(members) + "}";
}

std::string remote(std::string_view sha = "3333333333333333333333333333333333333333333333333333333333333333") {
    return R"("urls":["https://cdn.test/a"],"sha256":")" + std::string(sha) + R"(","size":4)";
}

void check_malformed(std::string_view text, std::string_view field) {
    const auto parsed = parse(text);
    REQUIRE_FALSE(parsed);
    CHECK(parsed.error().id == "components.manifest_malformed");
    CHECK(arg_text(parsed.error(), "field") == field);
}

}  // namespace

TEST_CASE("the golden manifest parses every field and skips what this build cannot name", "[components][manifest]") {
    const auto parsed = parse(read_data("release_manifest.json"));
    REQUIRE(parsed);
    const ReleaseManifest& manifest = *parsed;
    CHECK(manifest.schema == VersionStreams::manifest_schema);
    CHECK(manifest.serial == 12);
    CHECK(manifest.expires_at == std::chrono::system_clock::time_point(std::chrono::milliseconds(1792592000000)));

    REQUIRE(manifest.apps.size() == 2);
    const AppEntry& windows = manifest.apps[0];
    CHECK(windows.platform == ManifestPlatform{ManifestOs::Windows, ManifestArch::X64});
    CHECK(windows.channel == "stable");
    CHECK(windows.version.to_string() == "1.4.0");
    REQUIRE(windows.min_supported);
    CHECK(windows.min_supported->to_string() == "1.2.0");
    CHECK(windows.kind == AppPackageKind::Velopack);
    CHECK(windows.package.urls.size() == 2);
    CHECK(windows.package.size == 81234567);
    CHECK(windows.package.sha256[0] == 0x00);
    CHECK(windows.package.sha256[31] == 0xFF);
    CHECK_FALSE(windows.downgrade_ok);
    const AppEntry& linux_beta = manifest.apps[1];
    CHECK(linux_beta.kind == AppPackageKind::Tarball);
    CHECK(linux_beta.downgrade_ok);
    CHECK_FALSE(linux_beta.min_supported);

    REQUIRE(manifest.payloads.size() == 1);
    const PayloadEntry& payload = manifest.payloads[0];
    CHECK(payload.version.to_string() == "3.1.0");
    CHECK(payload.payload_abi == 1);
    REQUIRE(payload.files.size() == 2);
    REQUIRE(payload.find(PayloadRole::ClientDll) != nullptr);
    REQUIRE(payload.find(PayloadRole::Winhost) != nullptr);
    CHECK(payload.find(PayloadRole::Winhost)->file.size == 524288);

    REQUIRE(manifest.runtimes.size() == 2);
    CHECK(manifest.runtimes[0].id == "kron4ek-wine-10.0-x64");
    CHECK(manifest.runtimes[0].kind == RuntimeKind::KronWine);
    CHECK(manifest.runtimes[0].platform == ManifestPlatform{ManifestOs::Linux, ManifestArch::X64});
    CHECK(manifest.runtimes[1].kind == RuntimeKind::VcRedist);
    CHECK_FALSE(manifest.runtimes[1].platform);

    REQUIRE(manifest.endpoint);
    CHECK(manifest.endpoint->host == "sb.example.org");
    CHECK(manifest.endpoint->port == Port{8443});
}

TEST_CASE("an unknown schema is refused before anything else is read", "[components][manifest]") {
    const auto parsed = parse(R"({"schema":2,"serial":"not a number"})");
    REQUIRE_FALSE(parsed);
    CHECK(parsed.error().id == "components.manifest_schema_unsupported");
    CHECK(parsed.error().kind == ErrorKind::Unsupported);
    CHECK(arg_text(parsed.error(), "schema") == "2");
}

TEST_CASE("a body that is not a JSON object is malformed", "[components][manifest]") {
    check_malformed("not json", "manifest");
    check_malformed("[1,2]", "manifest");
    check_malformed(R"({"serial":1})", "schema");
    check_malformed(R"({"schema":1,"expires_unix_ms":1})", "serial");
    check_malformed(R"({"schema":1,"serial":1})", "expires_unix_ms");
    check_malformed(R"({"schema":1,"serial":1,"expires_unix_ms":-1})", "expires_unix_ms");
}

TEST_CASE("an expiry past what system_clock can hold is malformed, never wrapped", "[components][manifest]") {
    constexpr u64 kFar = u64{1} << 52;
    const auto parsed = parse(R"({"schema":1,"serial":1,"expires_unix_ms":)" + std::to_string(kFar) + "}");
    if (parsed) {
        CHECK(std::chrono::duration_cast<std::chrono::milliseconds>(parsed->expires_at.time_since_epoch()).count() ==
              static_cast<i64>(kFar));
    } else {
        CHECK(arg_text(parsed.error(), "field") == "expires_unix_ms");
    }
}

TEST_CASE("every artifact needs urls and a sha256", "[components][manifest]") {
    const std::string app_prefix =
        R"("apps":[{"platform":{"os":"windows","arch":"x64"},"channel":"stable","version":"1.0.0","kind":"velopack","package":{)";
    check_malformed(with(app_prefix + R"("urls":["https://a"],"size":1}}])"), "apps[0].package.sha256");
    check_malformed(with(app_prefix + R"("urls":[],"sha256":"3333333333333333333333333333333333333333333333333333333333333333","size":1}}])"),
                    "apps[0].package.urls");
    check_malformed(with(app_prefix + R"("sha256":"3333333333333333333333333333333333333333333333333333333333333333","size":1}}])"),
                    "apps[0].package.urls");
    check_malformed(with(app_prefix + R"("urls":["https://a"],"sha256":"33","size":1}}])"), "apps[0].package.sha256");
    check_malformed(with(app_prefix + R"("urls":["https://a"],"sha256":"3333333333333333333333333333333333333333333333333333333333333333","size":0}}])"),
                    "apps[0].package.size");
}

TEST_CASE("two apps for one platform and channel, or an unknown app kind, are malformed", "[components][manifest]") {
    const std::string app = R"({"platform":{"os":"linux","arch":"x64"},"channel":"stable","version":"1.0.0","kind":"tarball","package":{)" +
                            remote() + "}}";
    check_malformed(with(R"("apps":[)" + app + "," + app + "]"), "apps[1]");
    const std::string other_channel =
        R"({"platform":{"os":"linux","arch":"x64"},"channel":"beta","version":"1.0.0","kind":"tarball","package":{)" + remote() + "}}";
    CHECK(parse(with(R"("apps":[)" + app + "," + other_channel + "]")));
    check_malformed(with(R"("apps":[{"platform":{"os":"linux","arch":"x64"},"channel":"stable","version":"1.0.0","kind":"appimage","package":{)" +
                         remote() + "}}]"),
                    "apps[0].kind");
}

TEST_CASE("payload versions, roles and the client DLL are checked", "[components][manifest]") {
    const std::string client = R"({"role":"client_dll",)" + remote() + "}";
    const std::string winhost = R"({"role":"winhost",)" + remote() + "}";
    const auto payload = [](std::string_view version, std::string_view files) {
        return R"({"version":")" + std::string(version) + R"(","payload_abi":1,"files":[)" + std::string(files) + "]}";
    };
    check_malformed(with(R"("payloads":[)" + payload("1.0.0", client) + "," + payload("1.0.0", client) + "]"),
                    "payloads[1].version");
    check_malformed(with(R"("payloads":[)" + payload("1.0.0", client + "," + client) + "]"), "payloads[0].files[1].role");
    check_malformed(with(R"("payloads":[)" + payload("1.0.0", winhost) + "]"), "payloads[0].files");
    check_malformed(with(R"("payloads":[{"version":"1.0.0","payload_abi":70000,"files":[)" + client + "]}]"),
                    "payloads[0].payload_abi");
    check_malformed(with(R"("payloads":[)" + payload("one", client) + "]"), "payloads[0].version");
    CHECK(parse(with(R"("payloads":[)" + payload("1.0.0", client + "," + winhost) + "," + payload("1.1.0", client) + "]")));
}

TEST_CASE("runtime ids are unique and only VcRedist has no platform", "[components][manifest]") {
    const auto runtime = [](std::string_view id, std::string_view kind, bool platform) {
        return R"({"id":")" + std::string(id) + R"(","kind":")" + std::string(kind) + R"(","version":"1","archive":{)" + remote() +
               "}" + (platform ? R"(,"platform":{"os":"macos","arch":"arm64"})" : "") + "}";
    };
    check_malformed(with(R"("runtimes":[)" + runtime("a", "mac_wine", true) + "," + runtime("a", "umu", true) + "]"),
                    "runtimes[1].id");
    // A skipped entry still claims its id.
    check_malformed(with(R"("runtimes":[)" + runtime("a", "warp_drive", true) + "," + runtime("a", "umu", true) + "]"),
                    "runtimes[1].id");
    check_malformed(with(R"("runtimes":[)" + runtime("vc", "vc_redist", true) + "]"), "runtimes[0].platform");
    check_malformed(with(R"("runtimes":[)" + runtime("wine", "mac_wine", false) + "]"), "runtimes[0].platform");
    check_malformed(with(R"("runtimes":[{"id":"","kind":"umu","version":"1","archive":{)" + remote() + "}}]"), "runtimes[0].id");

    const auto parsed = parse(with(R"("runtimes":[)" + runtime("wine", "mac_wine", true) + "," + runtime("vc", "vc_redist", false) + "]"));
    REQUIRE(parsed);
    REQUIRE(parsed->runtimes.size() == 2);
    CHECK(parsed->runtimes[0].platform == ManifestPlatform{ManifestOs::MacOs, ManifestArch::Arm64});
}

TEST_CASE("the endpoint override needs a host and a port", "[components][manifest]") {
    check_malformed(with(R"("endpoint":{"host":"","port":443})"), "endpoint.host");
    check_malformed(with(R"("endpoint":{"host":"sb.test","port":0})"), "endpoint.port");
    check_malformed(with(R"("endpoint":{"host":"sb.test","port":65536})"), "endpoint.port");
    check_malformed(with(R"("endpoint":{"host":"sb.test"})"), "endpoint.port");
    const auto parsed = parse(with(R"("endpoint":{"host":"sb.test","port":443})"));
    REQUIRE(parsed);
    CHECK(parsed->endpoint == EndpointOverride{"sb.test", Port{443}});
    CHECK_FALSE(parse(with(""))->endpoint);
}

TEST_CASE("check_payload_abi accepts only this build's ABI", "[components][manifest]") {
    PayloadEntry entry;
    entry.version = *SemVer::parse("2.0.0");
    entry.payload_abi = VersionStreams::payload_abi;
    CHECK(check_payload_abi(entry));
    entry.payload_abi = static_cast<u16>(VersionStreams::payload_abi + 1);
    const auto refused = check_payload_abi(entry);
    REQUIRE_FALSE(refused);
    CHECK(refused.error().id == "components.payload_abi_mismatch");
    CHECK(arg_text(refused.error(), "version") == "2.0.0");
    CHECK(arg_text(refused.error(), "expected") == std::to_string(VersionStreams::payload_abi));
}
