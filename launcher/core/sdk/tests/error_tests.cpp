#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <chrono>
#include <string>
#include <thread>
#include <vector>

#include "reboot/api/codec.hpp"
#include "reboot/api/v1/common.hpp"
#include "reboot/sdk/dispatcher.hpp"
#include "reboot/sdk/error.hpp"
#include "reboot/sdk/event.hpp"

namespace api = rb::api;
using namespace rb::sdk;
using namespace std::chrono_literals;

namespace {

[[nodiscard]] std::string render(std::string_view text, std::vector<ErrorArg> args) {
    return rb::sdk::detail::render_template(text, args, nullptr);
}

}  // namespace

TEST_CASE("sdk error: the English template of a known id takes its args and the detail") {
    api::Diagnostic diagnostic;
    diagnostic.id = "api.unknown_method";
    diagnostic.args = {{"method", api::ArgKind::Unsigned, "42"}};
    diagnostic.detail = "from a test";
    diagnostic.kind = api::ErrorKind::InvalidInput;
    const api::Bytes bytes = api::encode(diagnostic);
    const Error error = Error::decode(RB_E_REMOTE, bytes);
    CHECK(error.id() == "api.unknown_method");
    CHECK(error.status() == RB_E_REMOTE);
    CHECK(error.kind() == api::ErrorKind::InvalidInput);
    REQUIRE(error.arg("method") != nullptr);
    CHECK(*error.arg("method") == "42");
    CHECK(error.english() == "This engine has no method 42. (from a test)");
}

TEST_CASE("sdk error: an id with no template reads as the id, and garbage as sdk.malformed_reply") {
    CHECK(Error::local(RB_E_INTERNAL, "nobody.knows_this").english() == "nobody.knows_this");
    const std::vector<std::uint8_t> garbage{0xFF, 0xFF, 0xFF};
    const Error malformed = Error::decode(RB_E_REMOTE, garbage);
    CHECK(malformed.id() == "sdk.malformed_reply");
    CHECK(malformed.english() == "The engine sent a diagnostic this program could not read.");
}

TEST_CASE("sdk error: templates follow ICU quoting and English one/other plurals") {
    const std::vector<ErrorArg> one{{"count", api::ArgKind::Unsigned, "1"}, {"name", api::ArgKind::String, "Arena"}};
    const std::vector<ErrorArg> three{{"count", api::ArgKind::Unsigned, "3"}, {"name", api::ArgKind::String, "Arena"}};
    CHECK(render("{name} has {count, plural, one {# player} other {# players}}", one) == "Arena has 1 player");
    CHECK(render("{name} has {count, plural, one {# player} other {# players}}", three) == "Arena has 3 players");
    CHECK(render("the launcher's '{name}' is literal, '' is one quote", one) == "the launcher's {name} is literal, ' is one quote");
    CHECK(render("{missing} stays", one) == "{missing} stays");
    CHECK(render("waited {delay}", {{"delay", api::ArgKind::Millis, "1500"}}) == "waited 1500 ms");
}

TEST_CASE("sdk event: a kind without a payload decodes to its marker, and an unknown kind is skipped") {
    api::Event resync;
    resync.kind = api::EventKind::Resync;
    resync.epoch = 3;
    resync.seq = 9;
    const std::optional<Event> decoded = decode_event(api::encode(resync));
    REQUIRE(decoded);
    CHECK(decoded->get<Resync>() != nullptr);
    CHECK(decoded->epoch == 3);

    api::Event unknown;
    unknown.kind = static_cast<api::EventKind>(9999);
    CHECK_FALSE(decode_event(api::encode(unknown)));

    api::EventPayload payload;
    payload.op_progress = api::Progress{};
    payload.op_progress->op_id = 5;
    api::Event progress;
    progress.kind = api::EventKind::OpProgress;
    progress.op_id = 5;
    progress.payload = api::encode(payload);
    const std::optional<Event> typed = decode_event(api::encode(progress));
    REQUIRE(typed);
    REQUIRE(typed->get<api::Progress>() != nullptr);
    CHECK(typed->get<api::Progress>()->op_id == 5);

    // A payload whose member disagrees with the kind is not trusted.
    progress.kind = api::EventKind::OpCompleted;
    CHECK_FALSE(decode_event(api::encode(progress)));
}

TEST_CASE("sdk dispatcher: a queue runs what is posted from other threads, and a thread dispatcher runs it alone") {
    QueueDispatcher queue;
    int ran = 0;
    std::thread poster([&queue, &ran] {
        for (int i = 0; i < 3; ++i) queue.post([&ran] { ++ran; });
    });
    CHECK(queue.run_until([&ran] { return ran == 3; }, 5s));
    poster.join();
    CHECK_FALSE(queue.run_for(10ms));

    std::atomic<int> counted{0};
    {
        ThreadDispatcher thread;
        for (int i = 0; i < 100; ++i) thread.post([&counted] { counted.fetch_add(1); });
    }
    CHECK(counted.load() == 100);
}
