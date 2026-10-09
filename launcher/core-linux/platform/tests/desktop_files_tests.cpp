#include <catch2/catch_test_macros.hpp>

#include <string>
#include <vector>

#include "desktop_files.hpp"
#include "systemd_units.hpp"

using rb::NativePath;
using namespace rb::os_linux::platform;
using Args = std::vector<std::string>;

TEST_CASE("Exec values quote reserved characters and keep field codes", "[desktop_files]") {
    CHECK(desktop_exec({"/opt/reboot/reboot", "--activate-url", "%u"}, {"%u"}) == "/opt/reboot/reboot --activate-url %u");
    CHECK(desktop_exec({"/home/ada/My Apps/Reboot.AppImage"}) == "\"/home/ada/My Apps/Reboot.AppImage\"");
    CHECK(desktop_exec({"/a/$HOME`x`\"q\""}) == "\"/a/\\\\$HOME\\\\`x\\\\`\\\\\"q\\\\\"\"");
    CHECK(desktop_exec({"100%"}) == "100%%");
    CHECK(desktop_exec({""}) == "\"\"");
}

TEST_CASE("an Exec value written and read back gives the same arguments", "[desktop_files]") {
    const Args args{"/home/ada/We ird/$x`y\"z\\w", "--flag=a b", "%u", "50%", ""};
    const std::string text = render_desktop_entry("Reboot Launcher", args, {"NoDisplay=true"});
    const DesktopEntryKeys keys = parse_desktop_entry(text);
    REQUIRE(keys.exec);
    CHECK(split_desktop_exec(*keys.exec) == args);
}

TEST_CASE("desktop entries are read from their own group only", "[desktop_files]") {
    const DesktopEntryKeys keys = parse_desktop_entry(
        "[Desktop Action other]\nExec=/wrong\n[Desktop Entry]\nName=X\nExec = /bin/true --x\nHidden=true\n"
        "X-GNOME-Autostart-enabled=false\n# Exec=/commented\n");
    CHECK(keys.exec == "/bin/true --x");
    CHECK(keys.hidden == "true");
    CHECK(keys.autostart_enabled == "false");
    CHECK(opted_out(keys));
    CHECK_FALSE(opted_out(parse_desktop_entry("[Desktop Entry]\nExec=x\nHidden=false\n")));
    CHECK_FALSE(split_desktop_exec("\"unbalanced"));
}

TEST_CASE("an opt-out survives a rewrite", "[desktop_files]") {
    DesktopEntryKeys keep;
    keep.autostart_enabled = "false";
    const std::string text = render_desktop_entry("Reboot Launcher engine", {"/x", "run"}, {}, keep);
    CHECK(opted_out(parse_desktop_entry(text)));
}

TEST_CASE("detail command lines quote what integration's splitter groups", "[desktop_files]") {
    CHECK(join_command({"/opt/reboot", "run", "--origin=service-manager"}) == "/opt/reboot run --origin=service-manager");
    CHECK(join_command({"/home/a b/x", "say \"hi\""}) == "\"/home/a b/x\" \"say \\\"hi\\\"\"");
}

TEST_CASE("the program of a command skips an env wrapper", "[desktop_files]") {
    CHECK(program_of({"/opt/reboot/bin/reboot-engine", "run"}) == NativePath{"/opt/reboot/bin/reboot-engine"});
    CHECK(program_of({"env", "REBOOT_LAUNCHER_HOME=/data", "/opt/x", "run"}) == NativePath{"/opt/x"});
    CHECK(program_of({"/usr/bin/env", "A=1", "B=2", "/opt/y"}) == NativePath{"/opt/y"});
    CHECK_FALSE(program_of({"env", "A=1"}));
    CHECK_FALSE(program_of({}));
}

TEST_CASE("mimeapps.list defaults are read, set and removed", "[desktop_files]") {
    const std::string text =
        "[Added Associations]\nx-scheme-handler/reboot=other.desktop;\n\n[Default Applications]\n"
        "text/html=firefox.desktop;\nx-scheme-handler/reboot=old.desktop;other.desktop;\n";
    CHECK(mimeapps_default(text, "x-scheme-handler/reboot") == "old.desktop");
    CHECK_FALSE(mimeapps_default(text, "x-scheme-handler/http"));

    const std::string replaced = mimeapps_with_default(text, "x-scheme-handler/reboot", "reboot-launcher-url.desktop");
    CHECK(mimeapps_default(replaced, "x-scheme-handler/reboot") == "reboot-launcher-url.desktop");
    CHECK(mimeapps_default(replaced, "text/html") == "firefox.desktop");
    CHECK(replaced.find("[Added Associations]\nx-scheme-handler/reboot=other.desktop;") != std::string::npos);

    const std::string removed = mimeapps_with_default(replaced, "x-scheme-handler/reboot", std::nullopt);
    CHECK_FALSE(mimeapps_default(removed, "x-scheme-handler/reboot"));
    CHECK(mimeapps_default(removed, "text/html") == "firefox.desktop");

    const std::string created = mimeapps_with_default("", "x-scheme-handler/reboot", "a.desktop");
    CHECK(created == "[Default Applications]\nx-scheme-handler/reboot=a.desktop;\n");
    const std::string appended = mimeapps_with_default("[Added Associations]\n", "x-scheme-handler/reboot", "a.desktop");
    CHECK(mimeapps_default(appended, "x-scheme-handler/reboot") == "a.desktop");
}

TEST_CASE("unit commands escape specifiers and quote where needed", "[systemd_units]") {
    CHECK(unit_quote("/opt/reboot/bin/reboot-engine") == "/opt/reboot/bin/reboot-engine");
    CHECK(unit_quote("/home/a b/x") == "\"/home/a b/x\"");
    CHECK(unit_quote("100%") == "100%%");
    CHECK(unit_quote("$HOME") == "$$HOME");
    CHECK(unit_quote("a\"b") == "\"a\\\"b\"");
    const Args args{"/home/a b/Reboot.AppImage", "engine", "run", "--origin=service-manager", "50%$x"};
    std::string exec;
    for (const std::string& arg : args) exec += (exec.empty() ? "" : " ") + unit_quote(arg);
    CHECK(split_unit_command(exec) == args);
    CHECK_FALSE(split_unit_command("\"open"));
}

TEST_CASE("engine units carry the socket path, modes and pinned XDG homes", "[systemd_units]") {
    const std::string socket = render_engine_socket("0123456789abcdef");
    CHECK(unit_value(socket, "ListenStream") == "%t/reboot-launcher/0123456789abcdef.sock");
    CHECK(unit_value(socket, "SocketMode") == "0600");
    CHECK(unit_value(socket, "DirectoryMode") == "0700");
    CHECK(unit_value(socket, "WantedBy") == "sockets.target");
    CHECK(engine_listen_stream("0123456789abcdef") == "%t/reboot-launcher/0123456789abcdef.sock");

    const std::string service = render_engine_service({"/home/ada/.local/share/reboot-launcher/app/bin/reboot-engine",
                                                       "run", "--origin=service-manager"},
                                                      "/home/ada/.local/share", "/home/ada/.cache", "/home/ada/.local/state");
    CHECK(unit_value(service, "Type") == "exec");
    CHECK(unit_value(service, "Restart") == "on-failure");
    CHECK(unit_value(service, "StartLimitBurst") == "3");
    CHECK(unit_value(service, "Requires") == std::string(kEngineSocketUnit));
    CHECK(service.find("Environment=\"XDG_DATA_HOME=/home/ada/.local/share\"") != std::string::npos);
    CHECK(service.find("Environment=\"XDG_STATE_HOME=/home/ada/.local/state\"") != std::string::npos);
    const auto exec = unit_value(service, "ExecStart");
    REQUIRE(exec);
    CHECK(split_unit_command(*exec) ==
          Args{"/home/ada/.local/share/reboot-launcher/app/bin/reboot-engine", "run", "--origin=service-manager"});
}
