#pragma once

#include <catch2/catch_message.hpp>
#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <initializer_list>
#include <string>
#include <string_view>
#include <utility>

#include "reboot/foundation/random.hpp"
#include "reboot/testing/conformance_report.hpp"
#include "reboot/testing/port_conformance.hpp"
#include "reboot/testing/scratch_dir.hpp"
#include "reboot/testing/wall_clock_waiter.hpp"

namespace reboot::os_linux::platform::test {

inline void require_passed(const testing::ConformanceReport& report) {
    INFO(report.suite() << ": " << report.describe());
    CHECK(report.passed());
}

// For checks a CI runner cannot meet (no desktop session, no GIO): every other check must pass.
inline void require_passed_except(const testing::ConformanceReport& report, std::initializer_list<std::string_view> allowed) {
    for (const testing::ConformanceCheck& check : report.checks()) {
        if (check.status != testing::CheckStatus::Failed) continue;
        bool excused = false;
        for (const std::string_view name : allowed) excused = excused || check.name == name;
        INFO(report.suite() << ": " << check.name << ": " << check.detail);
        CHECK(excused);
    }
}

struct Scratch {
    OsRandom random;
    testing::WallClockWaiter waiter;
    testing::ScratchDir dir;

    explicit Scratch(std::string_view prefix = "reboot-linux-conformance") : dir(make_dir(random, prefix)) {}

    [[nodiscard]] testing::ConformanceEnv env() {
        return testing::ConformanceEnv{waiter, dir.path(), std::chrono::milliseconds{10000}};
    }

private:
    static testing::ScratchDir make_dir(IRandom& random, std::string_view prefix) {
        Result<testing::ScratchDir> created = testing::ScratchDir::create(random, prefix);
        REQUIRE(created);
        return std::move(*created);
    }
};

}  // namespace reboot::os_linux::platform::test
