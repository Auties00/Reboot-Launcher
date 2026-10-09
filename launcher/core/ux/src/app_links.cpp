#include "reboot/ux/app_links.hpp"

#include <array>
#include <span>

#include "links.hpp"
#include "messages.hpp"

namespace reboot::ux {

namespace {

constexpr std::array<LocalizedUrl, 1> kBugReport{
    LocalizedUrl{"en", "https://github.com/Auties00/Reboot-Launcher/issues/new"}};
constexpr std::array<LocalizedUrl, 1> kReleases{
    LocalizedUrl{"en", "https://github.com/Auties00/Reboot-Launcher/releases"}};
constexpr std::array<LocalizedUrl, 1> kDiscord{LocalizedUrl{"en", "https://discord.gg/rebootmp"}};

constexpr std::array<AppLink, 3> kOrder{AppLink::BugReport, AppLink::Releases, AppLink::Discord};

[[nodiscard]] std::span<const LocalizedUrl> urls_of(AppLink link) {
    switch (link) {
        case AppLink::BugReport: return kBugReport;
        case AppLink::Releases: return kReleases;
        case AppLink::Discord: return kDiscord;
    }
    return {};
}

}  // namespace

MessageId app_link_label(AppLink link) {
    switch (link) {
        case AppLink::BugReport: return msg::kLinkBugReport;
        case AppLink::Releases: return msg::kLinkReleases;
        case AppLink::Discord: return msg::kLinkDiscord;
    }
    return msg::kLinkBugReport;
}

std::string AppLinks::url(AppLink link, const LanguageTag& language) const {
    return localized_url(urls_of(link), language);
}

std::vector<AppLinkEntry> AppLinks::list(const LanguageTag& language) const {
    std::vector<AppLinkEntry> out;
    for (const AppLink link : kOrder) out.push_back({link, app_link_label(link), url(link, language)});
    return out;
}

}  // namespace reboot::ux
