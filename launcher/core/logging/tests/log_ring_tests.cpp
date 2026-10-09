#include <catch2/catch_test_macros.hpp>
#include <string>
#include <vector>

#include "reboot/logging/log_filter.hpp"
#include "reboot/logging/log_ring.hpp"
#include "reboot/logging/wine_log_files.hpp"

using namespace rb;
using namespace rb::logging;

namespace {

LogRecord record(std::string text, LogLevel level = LogLevel::Info, LogCategory category = LogCategory::Engine) {
    LogRecord out;
    out.level = level;
    out.category = category;
    out.text = std::move(text);
    return out;
}

SessionId session_of(u8 fill) {
    SessionId session;
    session.value.bytes.fill(fill);
    return session;
}

void write_all(LogRing& ring, std::vector<LogRecord> records) { ring.write(records); }

std::vector<std::string> texts(const LogPage& page) {
    std::vector<std::string> out;
    for (const LogEntry& entry : page.entries) out.push_back(entry.record.text);
    return out;
}

}  // namespace

TEST_CASE("filters match level, categories and session", "[logging][filter]") {
    LogRecord engine = record("e", LogLevel::Warn);
    LogFilter filter;
    CHECK(filter.matches(engine));

    filter.min_level = LogLevel::Error;
    CHECK_FALSE(filter.matches(engine));

    filter = {};
    filter.categories = {LogCategory::Play, LogCategory::Engine};
    CHECK(filter.matches(engine));
    filter.categories = {LogCategory::Play};
    CHECK_FALSE(filter.matches(engine));

    filter = {};
    filter.session = session_of(1);
    CHECK_FALSE(filter.matches(engine));
    engine.session = session_of(1);
    CHECK(filter.matches(engine));
    engine.session = session_of(2);
    CHECK_FALSE(filter.matches(engine));
}

TEST_CASE("Wine output with a session is routed away from the ring", "[logging][ring]") {
    LogRecord wine = record("w", LogLevel::Info, LogCategory::Wine);
    CHECK_FALSE(routes_to_wine_log(wine));
    wine.session = session_of(3);
    CHECK(routes_to_wine_log(wine));

    LogRing ring;
    write_all(ring, {record("a"), wine, record("b")});
    const LogPage page = ring.read({}, {}, 10);
    CHECK(texts(page) == std::vector<std::string>{"a", "b"});
    CHECK(page.entries[0].seq == 1);
    CHECK(page.entries[1].seq == 2);
}

TEST_CASE("reads page through the ring with a cursor", "[logging][ring]") {
    LogRing ring;
    write_all(ring, {record("1"), record("2"), record("3"), record("4"), record("5")});

    LogPage page = ring.read({}, {}, 2);
    CHECK(texts(page) == std::vector<std::string>{"1", "2"});
    CHECK(page.next.after_seq == 2);
    CHECK(page.missed == 0);

    page = ring.read(page.next, {}, 2);
    CHECK(texts(page) == std::vector<std::string>{"3", "4"});
    page = ring.read(page.next, {}, 2);
    CHECK(texts(page) == std::vector<std::string>{"5"});
    page = ring.read(page.next, {}, 2);
    CHECK(page.entries.empty());
    CHECK(page.next == ring.end());
}

TEST_CASE("the cursor moves past entries the filter skipped", "[logging][ring]") {
    LogRing ring;
    write_all(ring, {record("d", LogLevel::Debug), record("w", LogLevel::Warn), record("d2", LogLevel::Debug)});
    LogFilter warnings;
    warnings.min_level = LogLevel::Warn;
    const LogPage page = ring.read({}, warnings, 10);
    CHECK(texts(page) == std::vector<std::string>{"w"});
    CHECK(page.next.after_seq == 3);
}

TEST_CASE("evicted entries are counted as missed", "[logging][ring]") {
    const std::string text(100, 'x');
    LogRing ring(3 * (sizeof(LogEntry) + text.size()));
    for (int i = 0; i < 5; ++i) write_all(ring, {record(text)});

    const LogPage page = ring.read({}, {}, 10);
    CHECK(page.missed == 2);
    REQUIRE(page.entries.size() == 3);
    CHECK(page.entries.front().seq == 3);

    const LogPage caught_up = ring.read(LogCursor{3}, {}, 10);
    CHECK(caught_up.missed == 0);
    CHECK(caught_up.entries.size() == 2);
}

TEST_CASE("a read never exceeds the byte cap but always makes progress", "[logging][ring]") {
    const std::string big(kLogReadMaxBytes / 2 + 1, 'x');
    LogRing ring(8 * kLogReadMaxBytes);
    write_all(ring, {record(big), record(big), record(big)});

    LogPage page = ring.read({}, {}, 10);
    CHECK(page.entries.size() == 1);
    CHECK(page.next.after_seq == 1);
    page = ring.read(page.next, {}, 10);
    CHECK(page.entries.size() == 1);
}

TEST_CASE("a cursor from an earlier run reads from the end", "[logging][ring]") {
    LogRing ring;
    write_all(ring, {record("a")});
    const LogPage page = ring.read(LogCursor{999}, {}, 10);
    CHECK(page.entries.empty());
    CHECK(page.next == ring.end());
    write_all(ring, {record("b")});
    CHECK(texts(ring.read(page.next, {}, 10)) == std::vector<std::string>{"b"});
}

TEST_CASE("the append callback runs once per batch that added entries", "[logging][ring]") {
    LogRing ring;
    int calls = 0;
    ring.set_on_append([&calls] { ++calls; });
    write_all(ring, {record("a"), record("b")});
    LogRecord wine = record("w", LogLevel::Info, LogCategory::Wine);
    wine.session = session_of(1);
    write_all(ring, {wine});
    CHECK(calls == 1);

    ring.set_on_append({});
    write_all(ring, {record("c")});
    CHECK(calls == 1);
}
