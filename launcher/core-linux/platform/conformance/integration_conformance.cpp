#include <catch2/catch_test_macros.hpp>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "conformance_support.hpp"
#include "desktop_files.hpp"
#include "messages.hpp"
#include "reboot/os_linux/platform/linux_integration_registrar.hpp"
#include "reboot/os_linux/platform/tarball_layout.hpp"
#include "reboot/os_linux/platform/xdg_paths.hpp"

using namespace reboot;
using namespace reboot::os_linux::platform;
using reboot::os_linux::platform::test::Scratch;
using ports::IntegrationKind;
using ports::IntegrationState;
namespace fs = std::filesystem;

namespace {

// Sets environment variables for one test and restores them after.
class EnvOverride {
public:
    explicit EnvOverride(std::vector<std::pair<std::string, std::string>> values) {
        for (auto& [name, value] : values) {
            const char* const old = std::getenv(name.c_str());
            saved_.emplace_back(name, old != nullptr ? std::optional<std::string>(old) : std::nullopt);
            ::setenv(name.c_str(), value.c_str(), 1);
        }
    }
    ~EnvOverride() {
        for (const auto& [name, value] : saved_) {
            if (value)
                ::setenv(name.c_str(), value->c_str(), 1);
            else
                ::unsetenv(name.c_str());
        }
    }
    EnvOverride(const EnvOverride&) = delete;
    EnvOverride& operator=(const EnvOverride&) = delete;

private:
    std::vector<std::pair<std::string, std::optional<std::string>>> saved_;
};

[[nodiscard]] std::string read(const NativePath& path) {
    std::ifstream in(path);
    return {std::istreambuf_iterator<char>(in), {}};
}

void append(const NativePath& path, std::string_view text) { std::ofstream(path, std::ios::app) << text; }

// An AppImage install whose XDG homes live in the scratch directory; the test binary's own
// directory stands in for the mount.
struct AppImageInstall {
    Scratch scratch{"reboot-linux-integration"};
    NativePath image = scratch.dir.path() / "Reboot Launcher.AppImage";
    std::optional<EnvOverride> env;
    std::optional<XdgPaths> paths;

    AppImageInstall() {
        std::ofstream{image} << "ELF";
        const Result<XdgPaths> plain = XdgPaths::detect();
        REQUIRE(plain);
        env.emplace(std::vector<std::pair<std::string, std::string>>{
            {"APPIMAGE", image.native()},
            {"APPDIR", plain->exe_dir().native()},
            {"XDG_CONFIG_HOME", (scratch.dir.path() / "config").native()},
            {"XDG_DATA_HOME", (scratch.dir.path() / "data").native()},
            {"XDG_DATA_DIRS", (scratch.dir.path() / "system").native()},
        });
        Result<XdgPaths> detected = XdgPaths::detect();
        REQUIRE(detected);
        paths.emplace(std::move(*detected));
        REQUIRE(paths->install_kind() == ports::InstallKind::AppImage);
    }
};

}  // namespace

TEST_CASE("an AppImage registers its engine through $APPIMAGE", "[linux_conformance]") {
    AppImageInstall install;
    const Result<EngineCommand> engine = stable_engine_command(*install.paths);
    REQUIRE(engine);
    CHECK(engine->exe == install.image);
    CHECK(engine->leading_args == std::vector<std::string>{"engine"});
}

TEST_CASE("the menu entry and autostart are written, judged and removed", "[linux_conformance]") {
    AppImageInstall install;
    LinuxIntegrationRegistrar registrar(*install.paths, "0123456789abcdef", std::nullopt);
    const std::vector<IntegrationKind> kinds{IntegrationKind::DesktopEntry, IntegrationKind::Autostart};
    test::require_passed(testing::run_integration_registrar_conformance(registrar, install.image, kinds));

    REQUIRE(registrar.apply(IntegrationKind::Autostart, install.image));
    const auto ours = registrar.status(IntegrationKind::Autostart);
    REQUIRE(ours);
    CHECK(ours->state == IntegrationState::Ours);
    CHECK(ours->detail == "\"" + install.image.native() + "\" engine run --origin=service-manager");

    // The user's opt-out survives a rewrite and reads as disabled.
    const NativePath entry = install.paths->config_home() / "autostart" / "reboot-launcher-engine.desktop";
    append(entry, "X-GNOME-Autostart-enabled=false\n");
    REQUIRE(registrar.apply(IntegrationKind::Autostart, install.image));
    CHECK(read(entry).find("X-GNOME-Autostart-enabled=false") != std::string::npos);
    const auto disabled = registrar.status(IntegrationKind::Autostart);
    REQUIRE(disabled);
    CHECK(disabled->detail == "disabled");

    // A moved AppImage leaves the entry stale.
    fs::rename(install.image, install.scratch.dir.path() / "moved.AppImage");
    const auto stale = registrar.status(IntegrationKind::Autostart);
    REQUIRE(stale);
    CHECK(stale->state == IntegrationState::Stale);
    REQUIRE(registrar.remove(IntegrationKind::Autostart));
    CHECK(registrar.status(IntegrationKind::Autostart)->state == IntegrationState::Absent);
}

TEST_CASE("an overridden data root reaches the autostart entry", "[linux_conformance]") {
    AppImageInstall install;
    LinuxIntegrationRegistrar registrar(*install.paths, "0123456789abcdef", NativePath{"/srv/reboot data"});
    REQUIRE(registrar.apply(IntegrationKind::Autostart, install.image));
    const DesktopEntryKeys keys =
        parse_desktop_entry(read(install.paths->config_home() / "autostart" / "reboot-launcher-engine.desktop"));
    REQUIRE(keys.exec);
    CHECK(split_desktop_exec(*keys.exec) ==
          std::vector<std::string>{"env", "REBOOT_LAUNCHER_HOME=/srv/reboot data", install.image.native(), "engine",
                                   "run", "--origin=service-manager"});
    CHECK(registrar.status(IntegrationKind::Autostart)->state == IntegrationState::Ours);
}

TEST_CASE("the reboot:// handler is ours until another application owns it", "[linux_conformance]") {
    AppImageInstall install;
    LinuxIntegrationRegistrar registrar(*install.paths, "0123456789abcdef", std::nullopt);
    REQUIRE(registrar.apply(IntegrationKind::UrlScheme, install.image));
    const auto ours = registrar.status(IntegrationKind::UrlScheme);
    REQUIRE(ours);
    CHECK(ours->state == IntegrationState::Ours);
    CHECK(ours->detail.ends_with("--activate-url %u"));
    REQUIRE(registrar.remove(IntegrationKind::UrlScheme));
    CHECK(registrar.status(IntegrationKind::UrlScheme)->state == IntegrationState::Absent);

    const NativePath applications = install.paths->data_home() / "applications";
    fs::create_directories(applications);
    std::ofstream{applications / "other.desktop"} << "[Desktop Entry]\nType=Application\nName=Other\nExec=/usr/bin/other %u\n";
    fs::create_directories(install.paths->config_home());
    std::ofstream{install.paths->config_home() / "mimeapps.list"}
        << "[Default Applications]\nx-scheme-handler/reboot=other.desktop;\n";
    const auto foreign = registrar.status(IntegrationKind::UrlScheme);
    REQUIRE(foreign);
    CHECK(foreign->state == IntegrationState::Foreign);
    CHECK(foreign->detail == "/usr/bin/other %u");
    const auto refused = registrar.apply(IntegrationKind::UrlScheme, install.image);
    REQUIRE_FALSE(refused);
    CHECK(refused.error().is(kIntegrationForeign));
    REQUIRE(registrar.remove(IntegrationKind::UrlScheme));
    CHECK(fs::exists(applications / "other.desktop"));
}
