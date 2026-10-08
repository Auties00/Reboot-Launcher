#include <catch2/catch_test_macros.hpp>
#include <chrono>

#include "reboot/logging/log_file_names.hpp"

using namespace reboot;
using namespace reboot::logging;
using namespace std::chrono;

namespace {

const LogFileGroup kGroup{sys_days{year{2026} / 10 / 8} + hours{13} + minutes{5} + seconds{9}, 4242};

SessionId session_of(u8 fill) {
    SessionId session;
    session.value.bytes.fill(fill);
    return session;
}

}  // namespace

TEST_CASE("log roles are 1 to 16 of [a-z0-9]", "[logging][names]") {
    CHECK(is_valid_log_role("engine"));
    CHECK(is_valid_log_role("cli2"));
    CHECK(is_valid_log_role("abcdefghijklmnop"));
    CHECK_FALSE(is_valid_log_role(""));
    CHECK_FALSE(is_valid_log_role("abcdefghijklmnopq"));
    CHECK_FALSE(is_valid_log_role("game-server"));
    CHECK_FALSE(is_valid_log_role("Engine"));
    CHECK_FALSE(is_valid_log_role("en.gine"));
}

TEST_CASE("session file names put the part before .log", "[logging][names]") {
    CHECK(session_log_file_name(kGroup, "engine", 0) == "launcher-20261008T130509Z-4242-engine.log");
    CHECK(session_log_file_name(kGroup, "engine", 3) == "launcher-20261008T130509Z-4242-engine.3.log");
}

TEST_CASE("Wine file names carry the run's group and the session", "[logging][names]") {
    CHECK(wine_log_file_name(kGroup, session_of(0xab)) ==
          "wine-20261008T130509Z-4242-abababab-abab-abab-abab-abababababab.log");
}

TEST_CASE("generated names classify back to their kind and group", "[logging][names]") {
    for (const u32 part : {0u, 1u, 12u}) {
        const auto parsed = classify_log_file(session_log_file_name(kGroup, "engine", part));
        REQUIRE(parsed);
        CHECK(parsed->kind == LogFileKind::Session);
        CHECK(parsed->group == kGroup);
    }
    const auto wine = classify_log_file(wine_log_file_name(kGroup, session_of(0x01)));
    REQUIRE(wine);
    CHECK(wine->kind == LogFileKind::Wine);
    CHECK(wine->group == kGroup);
}

TEST_CASE("Proton logs classify without a group", "[logging][names]") {
    const auto parsed = classify_log_file("steam-0.log");
    REQUIRE(parsed);
    CHECK(parsed->kind == LogFileKind::ProtonLog);
    CHECK_FALSE(parsed->group.has_value());
}

TEST_CASE("other files are never classified", "[logging][names]") {
    for (const char* name : {
             "launcher.log",
             "launcher-20261008T130509Z-4242-engine.txt",
             "launcher-20261008T130509Z-4242-engine.0.log",
             "launcher-20261008T130509Z-4242-engine.01.log",
             "launcher-20261008T130509Z-04242-engine.log",
             "launcher-20261308T130509Z-4242-engine.log",
             "launcher-20260230T130509Z-4242-engine.log",
             "launcher-20261008T250509Z-4242-engine.log",
             "launcher-20261008T130509Z-4242-Engine.log",
             "launcher-20261008T130509Z-4242-.log",
             "launcher-20261008T130509Z-99999999999-engine.log",
             "wine-20261008T130509Z-4242-not-a-uuid.log",
             "wine-20261008T130509Z-4242-ABABABAB-ABAB-ABAB-ABAB-ABABABABABAB.log",
             "steam-.log",
             "steam-12a.log",
             "notes.log",
         })
        CHECK_FALSE(classify_log_file(name).has_value());
}
