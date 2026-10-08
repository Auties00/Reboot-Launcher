#include <catch2/catch_test_macros.hpp>

#include <optional>
#include <string>

#include "reboot/components/release_manifest.hpp"
#include "reboot/updates/update_offer.hpp"

using namespace reboot;
using namespace reboot::updates;
using components::AppEntry;
using components::ManifestArch;
using components::ManifestOs;
using components::ManifestPlatform;
using storage::UpdateChannel;

namespace {

constexpr ManifestPlatform kLinux{ManifestOs::Linux, ManifestArch::X64};

AppEntry app(std::string channel, SemVer version) {
    AppEntry entry;
    entry.platform = kLinux;
    entry.channel = std::move(channel);
    entry.version = std::move(version);
    return entry;
}

const SemVer kInstalled{11, 0, 0, ""};

}  // namespace

TEST_CASE("each channel is offered its own entry", "[updates]") {
    components::ReleaseManifest manifest;
    manifest.apps = {app("stable", SemVer{11, 1, 0, ""}), app("beta", SemVer{11, 2, 0, "beta.1"})};

    const std::optional<UpdateOffer> stable = select_offer(manifest, kLinux, UpdateChannel::Stable, kInstalled);
    REQUIRE(stable);
    CHECK(stable->entry.version == SemVer{11, 1, 0, ""});

    const std::optional<UpdateOffer> beta = select_offer(manifest, kLinux, UpdateChannel::Beta, kInstalled);
    REQUIRE(beta);
    CHECK(beta->entry.version == SemVer{11, 2, 0, "beta.1"});
}

TEST_CASE("beta users still get a newer stable release", "[updates]") {
    components::ReleaseManifest manifest;
    manifest.apps = {app("stable", SemVer{11, 3, 0, ""}), app("beta", SemVer{11, 2, 0, "beta.1"})};
    const std::optional<UpdateOffer> offer = select_offer(manifest, kLinux, UpdateChannel::Beta, kInstalled);
    REQUIRE(offer);
    CHECK(offer->entry.channel == "stable");
}

TEST_CASE("a stable rollback does not reach beta users", "[updates]") {
    AppEntry rollback = app("stable", SemVer{10, 9, 0, ""});
    rollback.downgrade_ok = true;
    components::ReleaseManifest manifest;
    manifest.apps = {rollback};
    CHECK(select_offer(manifest, kLinux, UpdateChannel::Stable, kInstalled));
    CHECK_FALSE(select_offer(manifest, kLinux, UpdateChannel::Beta, kInstalled));
}

TEST_CASE("other platforms and the installed version are not offered", "[updates]") {
    AppEntry windows = app("stable", SemVer{12, 0, 0, ""});
    windows.platform = ManifestPlatform{ManifestOs::Windows, ManifestArch::X64};
    components::ReleaseManifest manifest;
    manifest.apps = {windows, app("stable", kInstalled)};
    CHECK_FALSE(select_offer(manifest, kLinux, UpdateChannel::Stable, kInstalled));
}

TEST_CASE("below min_supported the offer is required", "[updates]") {
    AppEntry entry = app("stable", SemVer{11, 2, 0, ""});
    entry.min_supported = SemVer{11, 1, 0, ""};
    components::ReleaseManifest manifest;
    manifest.apps = {entry};
    const std::optional<UpdateOffer> offer = select_offer(manifest, kLinux, UpdateChannel::Stable, kInstalled);
    REQUIRE(offer);
    CHECK(offer->required);
}
