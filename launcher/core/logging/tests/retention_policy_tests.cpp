#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <vector>

#include "reboot/logging/retention_policy.hpp"

using namespace reboot;
using namespace reboot::logging;
using namespace std::chrono;

namespace {

const system_clock::time_point kNow = sys_days{year{2026} / 10 / 8};

LogFileGroup group_at(int days_ago) {
    return LogFileGroup{floor<seconds>(kNow - days{days_ago}), static_cast<u32>(100 + days_ago)};
}

LogFileInfo session_file(const char* path, int days_ago, u64 size = 10) {
    return LogFileInfo{path, LogFileKind::Session, group_at(days_ago), size, kNow - days{days_ago}};
}

LogFileInfo proton_file(const char* path, int days_ago, u64 size = 10) {
    return LogFileInfo{path, LogFileKind::ProtonLog, std::nullopt, size, kNow - days{days_ago}};
}

}  // namespace

TEST_CASE("files within every limit are kept", "[logging][retention]") {
    const std::vector files{session_file("a", 1), session_file("b", 2), proton_file("p", 3)};
    CHECK(select_expired(files, RetentionPolicy{}, kNow, {}).empty());
}

TEST_CASE("files past max_age go, oldest first", "[logging][retention]") {
    const std::vector files{session_file("young", 1), proton_file("old-proton", 20), session_file("old", 15)};
    const auto expired = select_expired(files, RetentionPolicy{}, kNow, {});
    CHECK(expired == std::vector<NativePath>{"old-proton", "old"});
}

TEST_CASE("only the newest max_sessions groups are kept, with all their files", "[logging][retention]") {
    std::vector<LogFileInfo> files;
    for (int day = 0; day < 4; ++day) {
        files.push_back(session_file("", day));
        files.back().path = "s" + std::to_string(day);
        files.push_back(LogFileInfo{"w" + std::to_string(day), LogFileKind::Wine, group_at(day), 10, kNow - days{day}});
    }
    files.push_back(proton_file("p", 5));
    RetentionPolicy policy;
    policy.max_sessions = 2;
    const auto expired = select_expired(files, policy, kNow, {});
    CHECK(expired == std::vector<NativePath>{"s3", "w3", "s2", "w2"});
}

TEST_CASE("the byte cap removes the oldest files until the rest fit", "[logging][retention]") {
    const std::vector files{session_file("a", 1, 40), session_file("b", 2, 40), proton_file("p", 3, 40)};
    RetentionPolicy policy;
    policy.max_total_bytes = 50;
    CHECK(select_expired(files, policy, kNow, {}) == std::vector<NativePath>{"p", "b"});
}

TEST_CASE("files in use are never selected but still count", "[logging][retention]") {
    const std::vector files{session_file("live", 0, 40), session_file("old-live", 30, 40), session_file("b", 2, 40)};
    RetentionPolicy policy;
    policy.max_total_bytes = 70;
    const std::vector<NativePath> in_use{"live", "old-live"};
    CHECK(select_expired(files, policy, kNow, in_use) == std::vector<NativePath>{"b"});
}
