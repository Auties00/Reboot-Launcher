#include <optional>
#include <string>
#include <variant>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "reboot/ux/app_links.hpp"
#include "reboot/ux/doc_page.hpp"
#include "reboot/ux/help_ref.hpp"
#include "reboot/ux/language_tag.hpp"
#include "reboot/ux/suggested_action.hpp"

using namespace rb;
using namespace rb::ux;

namespace {

[[nodiscard]] LanguageTag tag(std::string_view text) {
    Result<LanguageTag> parsed = LanguageTag::parse(text);
    REQUIRE(parsed);
    return *parsed;
}

[[nodiscard]] std::optional<u64> port_arg(const MessageText& text, std::string_view name) {
    for (const auto& [arg_name, value] : text.args)
        if (arg_name == name) return std::get<u64>(value);
    return std::nullopt;
}

}  // namespace

TEST_CASE("AppLinks lists every product link with its label") {
    const AppLinks links;
    const std::vector<AppLinkEntry> entries = links.list(LanguageTag::english());
    REQUIRE(entries.size() == 3);
    CHECK(entries[0].link == AppLink::BugReport);
    CHECK(entries[0].label.id == "ux.link_bug_report");
    CHECK(entries[0].url == "https://github.com/Auties00/Reboot-Launcher/issues/new");
    CHECK(entries[1].link == AppLink::Releases);
    CHECK(entries[1].url == "https://github.com/Auties00/Reboot-Launcher/releases");
    CHECK(entries[2].link == AppLink::Discord);
    CHECK(entries[2].url == "https://discord.gg/rebootmp");
    for (const AppLinkEntry& entry : entries) CHECK(entry.url.starts_with("https://"));
}

TEST_CASE("AppLinks falls back to the en url for an untranslated language") {
    const AppLinks links;
    CHECK(links.url(AppLink::Discord, tag("de-DE")) == links.url(AppLink::Discord, LanguageTag::english()));
    CHECK(links.list(tag("ar")).size() == 3);
}

TEST_CASE("resolve_doc_url serves the en guide for every language") {
    const std::string en = resolve_doc_url(DocPage::PortForwardingGuide, LanguageTag::english());
    CHECK(en == "https://github.com/Auties00/Reboot-Launcher/blob/master/documentation/en/PortForwarding.md");
    CHECK(resolve_doc_url(DocPage::PortForwardingGuide, tag("en-GB")) == en);
    CHECK(resolve_doc_url(DocPage::PortForwardingGuide, tag("pt-BR")) == en);
    CHECK(doc_page_label(DocPage::PortForwardingGuide).id == "ux.link_port_forwarding_guide");
}

TEST_CASE("port_forwarding_help names one port or a range") {
    const HelpRef single = port_forwarding_help(7777, 7777);
    CHECK(single.text.id.id == "ux.help_port_forwarding_port");
    CHECK(port_arg(single.text, "port") == 7777);
    CHECK(single.doc == DocPage::PortForwardingGuide);

    const HelpRef range = port_forwarding_help(7777, 7780);
    CHECK(range.text.id.id == "ux.help_port_forwarding_port_range");
    CHECK(port_arg(range.text, "first_port") == 7777);
    CHECK(port_arg(range.text, "last_port") == 7780);

    const HelpRef reversed = port_forwarding_help(7780, 7777);
    CHECK(port_arg(reversed.text, "first_port") == 7777);
    CHECK(port_arg(reversed.text, "last_port") == 7780);
}

TEST_CASE("action_label names every suggested action") {
    CHECK(action_label(RemediatePrerequisite{"wine"}).id == "ux.action_remediate_prerequisite");
    CHECK(action_label(InstallBuild{}).id == "ux.action_install_build");
    CHECK(action_label(ImportBuild{}).id == "ux.action_import_build");
    CHECK(action_label(EditDisplayName{}).id == "ux.action_edit_display_name");
    CHECK(action_label(SetDefaultHostListing{true}).id == "ux.choice_list_publicly");
    CHECK(action_label(SetDefaultHostListing{false}).id == "ux.choice_keep_unlisted");
    CHECK(action_label(ListHostProfile{}).id == "ux.action_list_host_profile");
    CHECK(action_label(CopyShareLink{}).id == "ux.action_copy_share_link");
    CHECK(action_label(OpenAppLink{AppLink::Releases}).id == "ux.link_releases");
}
