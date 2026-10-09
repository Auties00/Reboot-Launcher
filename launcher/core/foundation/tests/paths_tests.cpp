#include <optional>
#include <string>

#include <catch2/catch_test_macros.hpp>

#include "reboot/foundation/diag.hpp"
#include "reboot/foundation/paths.hpp"
#include "reboot/foundation/sha256.hpp"
#include "reboot/testing/fake_platform_paths.hpp"

using namespace reboot;

namespace {

const testing::FakePlatformPaths& fake_paths() {
    static const testing::FakePlatformPaths paths;
    return paths;
}

NativePath fake_root() { return testing::default_fake_root(); }

}  // namespace

TEST_CASE("The data root is the platform default unless overridden", "[foundation][paths]") {
    const Result<DataRoot> platform = resolve_data_root(fake_paths(), std::nullopt);
    REQUIRE(platform);
    CHECK(platform->root == fake_root() / "data");
    CHECK_FALSE(platform->overridden);

    const Result<DataRoot> empty = resolve_data_root(fake_paths(), std::string_view{});
    REQUIRE(empty);
    CHECK_FALSE(empty->overridden);

    const std::string custom = (fake_root() / "custom").generic_string();
    const Result<DataRoot> overridden = resolve_data_root(fake_paths(), custom);
    REQUIRE(overridden);
    CHECK(overridden->overridden);
    CHECK(overridden->root == fake_root() / "custom");
}

TEST_CASE("A relative REBOOT_LAUNCHER_HOME is refused", "[foundation][paths]") {
    const Result<DataRoot> relative = resolve_data_root(fake_paths(), std::string_view("relative/home"));
    REQUIRE_FALSE(relative);
    CHECK(relative.error().id == "foundation.data_root_not_absolute");
    CHECK(relative.error().kind == ErrorKind::InvalidInput);
    CHECK(relative.error().find_arg("path") != nullptr);
}

TEST_CASE("A platform without a data folder is reported", "[foundation][paths]") {
    class NoRoot final : public ports::IPlatformPaths {
    public:
        [[nodiscard]] NativePath default_data_root() const override { return {}; }
        [[nodiscard]] NativePath default_cache_root() const override { return {}; }
        [[nodiscard]] NativePath default_logs_root() const override { return {}; }
        [[nodiscard]] NativePath ipc_runtime_base() const override { return {}; }
        [[nodiscard]] NativePath exe_dir() const override { return {}; }
        [[nodiscard]] ports::InstallKind install_kind() const override { return ports::InstallKind::Dev; }
        [[nodiscard]] std::optional<NativePath> velopack_package_dir() const override { return std::nullopt; }
    };
    const Result<DataRoot> none = resolve_data_root(NoRoot{}, std::nullopt);
    REQUIRE_FALSE(none);
    CHECK(none.error().id == "foundation.no_data_root");
}

TEST_CASE("canonical_root is absolute, normal and has no trailing separator", "[foundation][paths]") {
    const NativePath base = fake_root() / "a" / "b";
    const NativePath expected = canonical_root(DataRoot{base, true});
    CHECK(expected.is_absolute());
    CHECK(canonical_root(DataRoot{fake_root() / "a" / "x" / ".." / "b" / "", true}) == expected);
    CHECK(canonical_root(DataRoot{fake_root() / "a" / "." / "b", true}) == expected);
    CHECK(root_hash16(canonical_root(DataRoot{base / "", true})) == root_hash16(expected));
}

TEST_CASE("root_hash16 is the first 16 hex digits of sha256 over the path bytes", "[foundation][paths]") {
    const NativePath path = fake_root() / "data";
    const std::string hash = root_hash16(path);
    CHECK(hash.size() == 16);
    CHECK(hash.find_first_not_of("0123456789abcdef") == std::string::npos);
    const std::string utf8 = display_utf8(path);
    CHECK(hash == to_hex(sha256({reinterpret_cast<const u8*>(utf8.data()), utf8.size()})).substr(0, 16));
    CHECK(root_hash16(fake_root() / "other") != hash);
}

TEST_CASE("AppLayout puts cache and logs under an overridden root only", "[foundation][paths]") {
    const AppLayout platform(DataRoot{fake_root() / "data", false}, fake_paths());
    CHECK(platform.settings_file() == fake_root() / "data" / "config" / "settings.json");
    CHECK(platform.engine_lock() == fake_root() / "data" / "state" / "engine.lock");
    CHECK(platform.catalog_cache() == fake_root() / "cache" / "catalog.json");
    CHECK(platform.logs_dir() == fake_root() / "logs");

    const AppLayout overridden(DataRoot{fake_root() / "home", true}, fake_paths());
    CHECK(overridden.catalog_cache() == fake_root() / "home" / "cache" / "catalog.json");
    CHECK(overridden.logs_dir() == fake_root() / "home" / "logs");

    const SessionId session{Uuid{{0xAB}}};
    CHECK(platform.game_server_session_dir(session) ==
          fake_root() / "data" / "data" / "game-server" / format_uuid(session.value));
}

TEST_CASE("locate_install uses the exe directory unless a dev tree is given", "[foundation][paths]") {
    const InstallLayout installed = locate_install(fake_paths(), std::nullopt);
    CHECK(installed.install_dir == fake_root() / "app");
#if defined(_WIN32)
    CHECK(installed.backend_exe == fake_root() / "app" / "reboot-backend.exe");
    CHECK(installed.game_server_exe == fake_root() / "app" / "reboot-game-server.exe");
#else
    CHECK(installed.backend_exe == fake_root() / "app" / "reboot-backend");
    CHECK(installed.game_server_exe == fake_root() / "app" / "reboot-game-server");
#endif
    CHECK(installed.bundled_catalog == fake_root() / "app" / "catalog.json");
    CHECK(installed.bundled_manifest == fake_root() / "app" / "manifest.json");
    CHECK(installed.backend_content_dir == fake_root() / "app" / "backend-content");

    const InstallLayout dev = locate_install(fake_paths(), fake_root() / "build");
    CHECK(dev.install_dir == fake_root() / "build");
}

TEST_CASE("is_inside is lexical and a path is inside itself", "[foundation][paths]") {
    const NativePath root = fake_root();
    CHECK(is_inside(root / "a" / "b", root / "a"));
    CHECK(is_inside(root / "a", root / "a"));
    CHECK(is_inside(root / "a", root / "a" / ""));
    CHECK(is_inside(root / "a" / "x" / ".." / "b", root / "a"));
    CHECK_FALSE(is_inside(root / "ab", root / "a"));
    CHECK_FALSE(is_inside(root / "a" / ".." / "b", root / "a"));
    CHECK_FALSE(is_inside(root, root / "a"));
#if defined(_WIN32)
    CHECK(is_inside(NativePath(L"C:\\Users\\Me\\AppData\\Local\\RebootLauncher\\data"),
                    NativePath(L"c:/users/me/appdata/local/rebootlauncher")));
#else
    CHECK_FALSE(is_inside(root / "A" / "b", root / "a"));
#endif
}
