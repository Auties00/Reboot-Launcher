#include <catch2/catch_test_macros.hpp>

#include <optional>
#include <string>
#include <vector>

#include "reboot/integration/entry_command.hpp"

using namespace rb::integration;
using Tokens = std::vector<std::string>;

TEST_CASE("each flavour has its own link placeholder", "[integration][entry_command]") {
    CHECK(url_placeholder(EntryFlavor::Windows) == "%1");
    CHECK(url_placeholder(EntryFlavor::FreeDesktop) == "%u");
    CHECK_FALSE(url_placeholder(EntryFlavor::Apple));
}

TEST_CASE("the scheme and autostart have fixed trailing arguments", "[integration][entry_command]") {
    CHECK(expected_args(IntegrationKind::UrlScheme, EntryFlavor::Windows) == Tokens{"--activate-url", "%1"});
    CHECK(expected_args(IntegrationKind::UrlScheme, EntryFlavor::FreeDesktop) == Tokens{"--activate-url", "%u"});
    CHECK(expected_args(IntegrationKind::Autostart, EntryFlavor::Windows) ==
          Tokens{"run", "--origin=service-manager"});
}

TEST_CASE("bundle-declared entries and the agent carry no checked command", "[integration][entry_command]") {
    CHECK_FALSE(expected_args(IntegrationKind::UrlScheme, EntryFlavor::Apple));
    CHECK_FALSE(expected_args(IntegrationKind::Autostart, EntryFlavor::Apple));
    CHECK_FALSE(expected_args(IntegrationKind::EngineAgent, EntryFlavor::Windows));
    CHECK_FALSE(expected_args(IntegrationKind::DesktopEntry, EntryFlavor::FreeDesktop));
}

TEST_CASE("quotes group a program path with spaces", "[integration][entry_command]") {
    // MSVC mis-stringizes raw literals holding backslashes inside Catch's macros, so they stay outside.
    const std::string command = R"("C:\Program Files\Reboot\reboot.exe" --activate-url "%1")";
    const std::string program = R"(C:\Program Files\Reboot\reboot.exe)";
    CHECK(split_entry_command(command) == Tokens{program, "--activate-url", "%1"});
}

TEST_CASE("an escaped quote is literal and repeated spaces split once", "[integration][entry_command]") {
    const std::string escaped = R"(a  "b\"c"   d)";
    CHECK(split_entry_command(escaped) == Tokens{"a", R"(b"c)", "d"});
    CHECK(split_entry_command(R"(a "")") == Tokens{"a", ""});
}

TEST_CASE("an unbalanced quote or an empty text is no command", "[integration][entry_command]") {
    const std::string unbalanced = R"("C:\reboot.exe --activate-url %1)";
    CHECK_FALSE(split_entry_command(unbalanced));
    CHECK_FALSE(split_entry_command(""));
    CHECK_FALSE(split_entry_command("   "));
}
