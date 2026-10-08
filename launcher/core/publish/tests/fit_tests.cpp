#include <catch2/catch_test_macros.hpp>
#include <string>
#include <utility>

#include "messages.hpp"
#include "reboot/publish/field_limits.hpp"
#include "reboot/publish/host_metadata.hpp"
#include "reboot/publish/metadata_patch.hpp"
#include "reboot/publish/publish_request.hpp"

using namespace reboot;
using namespace reboot::publish;

namespace {

HostMetadata valid_metadata() {
    HostMetadata metadata;
    metadata.name = "Reboot server";
    metadata.description = "Arena";
    metadata.author = "Host123456";
    metadata.version = GameVersion{.major = 7, .minor = 40, .patch = std::nullopt};
    metadata.max_players = 100;
    return metadata;
}

PublishRequest valid_request() {
    PublishRequest request;
    request.metadata = valid_metadata();
    request.game_port = Port{7777};
    return request;
}

}  // namespace

TEST_CASE("fit_metadata keeps values within the rbsb/1 limits", "[publish]") {
    const auto fitted = fit_metadata(valid_metadata());
    REQUIRE(fitted);
    CHECK(*fitted == valid_metadata());
}

TEST_CASE("fit_metadata cuts long text on a code point boundary", "[publish]") {
    HostMetadata metadata = valid_metadata();
    // 63 ASCII bytes and a 2-byte e-acute cross the 64-byte name limit inside the code point.
    metadata.name = std::string(kMaxServerNameBytes - 1, 'a') + "\xC3\xA9";
    metadata.description = std::string(kMaxDescriptionBytes + 10, 'd');
    metadata.author = std::string(kMaxAuthorBytes + 1, 'h');

    const auto fitted = fit_metadata(std::move(metadata));
    REQUIRE(fitted);
    CHECK(fitted->name == std::string(kMaxServerNameBytes - 1, 'a'));
    CHECK(fitted->description.size() == kMaxDescriptionBytes);
    CHECK(fitted->author.size() == kMaxAuthorBytes);
}

TEST_CASE("fit_metadata refuses an empty name and too many players", "[publish]") {
    HostMetadata unnamed = valid_metadata();
    unnamed.name.clear();
    const auto empty = fit_metadata(std::move(unnamed));
    REQUIRE_FALSE(empty);
    CHECK(empty.error().is(msg::kServerNameEmpty));

    HostMetadata crowded = valid_metadata();
    crowded.max_players = kMaxPlayerLimit + 1;
    const auto too_many = fit_metadata(std::move(crowded));
    REQUIRE_FALSE(too_many);
    CHECK(too_many.error().is(msg::kMaxPlayersTooHigh));

    HostMetadata full = valid_metadata();
    full.max_players = kMaxPlayerLimit;
    CHECK(fit_metadata(std::move(full)));
}

TEST_CASE("fit_request checks the password, the game port and the player count", "[publish]") {
    CHECK(fit_request(valid_request()));

    PublishRequest long_password = valid_request();
    long_password.password = SecretString(std::string(kMaxPasswordBytes + 1, 'p'));
    const auto password = fit_request(std::move(long_password));
    REQUIRE_FALSE(password);
    CHECK(password.error().is(msg::kPasswordTooLong));

    PublishRequest longest_password = valid_request();
    longest_password.password = SecretString(std::string(kMaxPasswordBytes, 'p'));
    CHECK(fit_request(std::move(longest_password)));

    PublishRequest no_port = valid_request();
    no_port.game_port = Port{0};
    const auto port = fit_request(std::move(no_port));
    REQUIRE_FALSE(port);
    CHECK(port.error().is(msg::kGamePortMissing));

    PublishRequest crowded = valid_request();
    crowded.players = kMaxPlayerLimit + 1;
    const auto players = fit_request(std::move(crowded));
    REQUIRE_FALSE(players);
    CHECK(players.error().is(msg::kPlayerCountTooHigh));
}

TEST_CASE("fit_request fits the metadata it carries", "[publish]") {
    PublishRequest request = valid_request();
    request.metadata.name.clear();
    const auto fitted = fit_request(std::move(request));
    REQUIRE_FALSE(fitted);
    CHECK(fitted.error().is(msg::kServerNameEmpty));
}

TEST_CASE("fit_patch fits only the fields set, like fit_metadata", "[publish]") {
    CHECK(fit_patch(MetadataPatch{}));

    MetadataPatch long_text;
    long_text.name = std::string(kMaxServerNameBytes + 5, 'n');
    long_text.description = std::string(kMaxDescriptionBytes + 5, 'd');
    const auto fitted = fit_patch(std::move(long_text));
    REQUIRE(fitted);
    CHECK(fitted->name->size() == kMaxServerNameBytes);
    CHECK(fitted->description->size() == kMaxDescriptionBytes);
    CHECK_FALSE(fitted->max_players);

    MetadataPatch unnamed;
    unnamed.name = std::string();
    const auto empty = fit_patch(std::move(unnamed));
    REQUIRE_FALSE(empty);
    CHECK(empty.error().is(msg::kServerNameEmpty));

    MetadataPatch crowded;
    crowded.max_players = kMaxPlayerLimit + 1;
    const auto too_many = fit_patch(std::move(crowded));
    REQUIRE_FALSE(too_many);
    CHECK(too_many.error().is(msg::kMaxPlayersTooHigh));

    MetadataPatch long_password;
    long_password.password = SecretString(std::string(kMaxPasswordBytes + 1, 'p'));
    const auto password = fit_patch(std::move(long_password));
    REQUIRE_FALSE(password);
    CHECK(password.error().is(msg::kPasswordTooLong));

    MetadataPatch cleared_password;
    cleared_password.password = SecretString(std::string());
    CHECK(fit_patch(std::move(cleared_password)));
}

TEST_CASE("author_for is the display name, never the login", "[publish]") {
    storage::AccountRecord host;
    host.display_name = "Host123456";
    host.tag = "a1b2c3";
    CHECK(author_for(host) == "Host123456");
}
