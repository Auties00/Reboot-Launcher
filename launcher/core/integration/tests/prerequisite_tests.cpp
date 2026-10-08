#include <catch2/catch_test_macros.hpp>

#include <cstddef>

#include "reboot/integration/prerequisite_id.hpp"
#include "reboot/integration/prerequisite_spec.hpp"

using namespace reboot::integration;

TEST_CASE("every id round-trips through its stable string", "[integration][prerequisite]") {
    for (std::size_t i = 0; i < kPrerequisiteIds.size(); ++i) {
        const auto id = static_cast<PrerequisiteId>(i);
        CHECK(parse_prerequisite_id(to_string(id)) == id);
    }
}

TEST_CASE("unknown and retired ids do not parse", "[integration][prerequisite]") {
    CHECK_FALSE(parse_prerequisite_id("mac.metal3"));
    CHECK_FALSE(parse_prerequisite_id(""));
    CHECK_FALSE(parse_prerequisite_id("MAC.ROSETTA"));
}

TEST_CASE("each spec describes its own id", "[integration][prerequisite]") {
    for (std::size_t i = 0; i < kPrerequisiteIds.size(); ++i) {
        const auto id = static_cast<PrerequisiteId>(i);
        CHECK(prerequisite_spec(id).id == id);
        CHECK(prerequisite_spec(id).guidance.id.starts_with("integration.guide_"));
    }
}

TEST_CASE("only Rosetta installs, and hosting checks are advisory", "[integration][prerequisite]") {
    CHECK(prerequisite_spec(PrerequisiteId::MacRosetta).remedy == Remedy::Install);
    CHECK(prerequisite_spec(PrerequisiteId::LinuxLinger).remedy == Remedy::Enable);
    for (const PrerequisiteId id :
         {PrerequisiteId::MacAppFirewall, PrerequisiteId::MacLocalNetwork, PrerequisiteId::LinuxLinger})
        CHECK(prerequisite_spec(id).impact == PrerequisiteImpact::Advisory);
}
