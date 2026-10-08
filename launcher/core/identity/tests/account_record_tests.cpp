#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <string>

#include "reboot/identity/account_record.hpp"
#include "reboot/testing/fake_random.hpp"

using namespace reboot;
using namespace reboot::identity;

TEST_CASE("account_id is the display name, a dash and the tag", "[identity]") {
    const AccountRecord record{AccountRecordId{}, AccountRole::Client, "Bob", "a1b2c3"};
    CHECK(account_id(record) == "Bob-a1b2c3");
}

TEST_CASE("the longest account id fits kMaxAccountIdLength", "[identity]") {
    STATIC_REQUIRE(kMaxAccountIdLength == 23);
    const AccountRecord record{AccountRecordId{}, AccountRole::Host, std::string(kMaxDisplayNameLength, 'x'),
                               std::string(kTagLength, '0')};
    CHECK(account_id(record).size() == kMaxAccountIdLength);
}

TEST_CASE("generate_tag yields 6 chars of [a-z0-9]", "[identity]") {
    testing::FakeRandom random;
    for (int i = 0; i < 64; ++i) {
        const std::string tag = generate_tag(random);
        CHECK(tag.size() == kTagLength);
        CHECK(std::ranges::all_of(tag, [](char c) { return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9'); }));
    }
}

TEST_CASE("generate_tag repeats for the same seed", "[identity]") {
    testing::FakeRandom first(7);
    testing::FakeRandom second(7);
    CHECK(generate_tag(first) == generate_tag(second));
}
