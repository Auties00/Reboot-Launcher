#include <catch2/catch_test_macros.hpp>

#include <compare>

#include "reboot/compat/runtime_id.hpp"

using reboot::compat::compare_runtime_versions;

TEST_CASE("digit runs compare by value", "[compat][runtime_id]") {
    CHECK(std::is_gt(compare_runtime_versions("GE-Proton11-7", "GE-Proton10-25")));
    CHECK(std::is_lt(compare_runtime_versions("GE-Proton10-9", "GE-Proton10-25")));
    CHECK(std::is_gt(compare_runtime_versions("11.0.2", "11.0")));
    CHECK(std::is_lt(compare_runtime_versions("9.20", "11.0")));
}

TEST_CASE("equal versions compare equal", "[compat][runtime_id]") {
    CHECK(std::is_eq(compare_runtime_versions("24.0.1-dxmt0.6", "24.0.1-dxmt0.6")));
    CHECK(std::is_eq(compare_runtime_versions("", "")));
}

TEST_CASE("leading zeros keep a strong order", "[compat][runtime_id]") {
    CHECK(std::is_neq(compare_runtime_versions("11.01", "11.1")));
    CHECK((compare_runtime_versions("11.01", "11.1") < 0) == (compare_runtime_versions("11.1", "11.01") > 0));
}
