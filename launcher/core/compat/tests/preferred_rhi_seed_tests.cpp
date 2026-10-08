#include <catch2/catch_test_macros.hpp>

#include "reboot/compat/preferred_rhi_seed.hpp"
#include "test_data.hpp"

using namespace reboot::compat;

TEST_CASE("dx12 is rewritten to dx11", "[compat][rhi]") {
    const auto seeded = seed_preferred_rhi(test::read_text("rhi_dx12.ini"));
    REQUIRE(seeded);
    CHECK(*seeded == test::read_text("rhi_dx12.expected.ini"));
}

TEST_CASE("dx10 and dx11 are kept", "[compat][rhi]") {
    CHECK_FALSE(seed_preferred_rhi(test::read_text("rhi_dx10.ini")));
    CHECK_FALSE(seed_preferred_rhi(test::read_text("rhi_dx12.expected.ini")));
}

TEST_CASE("a missing key is added under the section", "[compat][rhi]") {
    const auto seeded = seed_preferred_rhi(test::read_text("rhi_no_key.ini"));
    REQUIRE(seeded);
    CHECK(*seeded == test::read_text("rhi_no_key.expected.ini"));
}

TEST_CASE("a missing section is appended", "[compat][rhi]") {
    const auto seeded = seed_preferred_rhi(test::read_text("rhi_no_section.ini"));
    REQUIRE(seeded);
    CHECK(*seeded == test::read_text("rhi_no_section.expected.ini"));
}

TEST_CASE("an empty file gets the section", "[compat][rhi]") {
    CHECK(seed_preferred_rhi("") == "[D3DRHIPreference]\nPreferredRHI=dx11\n");
}

TEST_CASE("a key in another section does not count", "[compat][rhi]") {
    const auto seeded = seed_preferred_rhi("[Other]\nPreferredRHI=dx11\n");
    REQUIRE(seeded);
    CHECK(*seeded == "[Other]\nPreferredRHI=dx11\n[D3DRHIPreference]\nPreferredRHI=dx11\n");
}
