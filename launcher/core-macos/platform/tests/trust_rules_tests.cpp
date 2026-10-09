#include <catch2/catch_test_macros.hpp>

#include <vector>

#include "trust_rules.hpp"

using namespace reboot::os_macos::platform;
using reboot::u8;

TEST_CASE("a certificate with empty trust settings is trusted as root", "[trust_rules]") {
    CHECK(classify_trust_settings({}) == TrustVerdict::Trusted);
}

TEST_CASE("the first entry that applies to SSL decides", "[trust_rules]") {
    const std::vector<TrustSettingsEntry> entries{
        {.applies_to_ssl = false, .constrained = false, .result = 3},
        {.applies_to_ssl = true, .constrained = true, .result = 3},
        {.applies_to_ssl = true, .constrained = false, .result = 4},
        {.applies_to_ssl = true, .constrained = false, .result = 3},
        {.applies_to_ssl = true, .constrained = false, .result = 1},
    };
    CHECK(classify_trust_settings(entries) == TrustVerdict::Denied);
}

TEST_CASE("an entry without a result trusts as root", "[trust_rules]") {
    const std::vector<TrustSettingsEntry> entries{{.applies_to_ssl = true, .constrained = false, .result = std::nullopt}};
    CHECK(classify_trust_settings(entries) == TrustVerdict::Trusted);
}

TEST_CASE("entries that never apply to SSL leave the certificate unspecified", "[trust_rules]") {
    const std::vector<TrustSettingsEntry> entries{{.applies_to_ssl = false, .constrained = false, .result = 1}};
    CHECK(classify_trust_settings(entries) == TrustVerdict::Unspecified);
}

TEST_CASE("a later domain overrides an earlier one", "[trust_rules]") {
    const std::vector<u8> system_root{1};
    const std::vector<u8> denied_by_admin{2};
    const std::vector<u8> user_added{3};
    const std::vector<u8> denied_then_trusted{4};
    const std::vector<CertificateVerdict> verdicts{
        {system_root, TrustVerdict::Trusted},          {denied_by_admin, TrustVerdict::Trusted},
        {denied_then_trusted, TrustVerdict::Trusted},  {denied_by_admin, TrustVerdict::Denied},
        {denied_then_trusted, TrustVerdict::Denied},   {user_added, TrustVerdict::Trusted},
        {denied_then_trusted, TrustVerdict::Trusted},  {system_root, TrustVerdict::Unspecified},
    };
    CHECK(trusted_anchors(verdicts) == std::vector<std::vector<u8>>{system_root, denied_then_trusted, user_added});
}
