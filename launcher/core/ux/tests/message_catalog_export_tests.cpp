#include <array>
#include <string>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "reboot/foundation/diag.hpp"
#include "reboot/ux/message_catalog.hpp"
#include "reboot/ux/message_catalog_export.hpp"

using namespace rb;
using namespace rb::ux;

namespace {

constexpr std::array<ArgSpec, 1> kNameArgs{ArgSpec{"name"}};
constexpr std::array<ArgSpec, 2> kPortArgs{ArgSpec{"first"}, ArgSpec{"last"}};

constexpr MessageSpec kHello{"ux.test_hello", "Hello {name}", kNameArgs};
constexpr MessageSpec kPorts{"net.test_ports", "Ports {first} to {last}", kPortArgs};
constexpr MessageSpec kPlain{"play.test_plain", "Nothing to fill in", {}};

const std::array<const MessageSpec*, 3> kRegistry{&kHello, &kPorts, &kPlain};

[[nodiscard]] std::vector<CatalogIssue> check(std::string_view json) {
    Result<std::vector<CatalogIssue>> issues = MessageCatalogExport(kRegistry).check(json);
    REQUIRE(issues);
    return *issues;
}

[[nodiscard]] std::vector<CatalogIssue> check_template(std::string_view key, std::string_view text) {
    std::string json = R"({"ux.test_hello": "Hello {name}", "net.test_ports": "Ports {first} to {last}",
                           "play.test_plain": "Nothing to fill in", ")";
    json += key;
    json += R"(": ")";
    json += text;
    json += R"("})";
    return check(json);
}

[[nodiscard]] bool has(const std::vector<CatalogIssue>& issues, CatalogIssueKind kind, std::string_view id) {
    for (const CatalogIssue& issue : issues)
        if (issue.kind == kind && issue.id == id) return true;
    return false;
}

}  // namespace

TEST_CASE("is_core_owned_key matches domain prefixes only") {
    CHECK(is_core_owned_key("ux.step_welcome_title"));
    CHECK(is_core_owned_key("game_channel.lost"));
    CHECK(is_core_owned_key("internal.bug"));
    CHECK_FALSE(is_core_owned_key("gui.sort_by"));
    CHECK_FALSE(is_core_owned_key("ux"));
    CHECK_FALSE(is_core_owned_key("findSetting"));
    CHECK_FALSE(is_core_owned_key(".ux"));
    CHECK_FALSE(is_core_owned_key("@ux.step_welcome_title"));
}

TEST_CASE("render writes the registry, keeps UI-owned keys and drops orphans") {
    const std::string existing = R"({
        "@@locale": "de",
        "ux.test_hello": "Stale {name}",
        "ux.removed": "Gone",
        "@ux.removed": {"description": "orphan metadata"},
        "gui.sort_by": "Sort by",
        "@gui.sort_by": {"description": "Browser sort label"},
        "@gui.missing": {"description": "metadata without a message"},
        "timeLeft": "{count, plural, one {# minute} other {# minutes}} left"
    })";
    const Result<std::string> rendered = MessageCatalogExport(kRegistry).render(existing);
    REQUIRE(rendered);
    CHECK(*rendered == R"({
  "@@locale": "en",
  "gui.sort_by": "Sort by",
  "@gui.sort_by": {"description":"Browser sort label"},
  "net.test_ports": "Ports {first} to {last}",
  "play.test_plain": "Nothing to fill in",
  "timeLeft": "{count, plural, one {# minute} other {# minutes}} left",
  "ux.test_hello": "Hello {name}"
}
)");
    const Result<std::vector<CatalogIssue>> issues = MessageCatalogExport(kRegistry).check(*rendered);
    REQUIRE(issues);
    CHECK(issues->empty());
}

TEST_CASE("render without an existing catalog and its JSON escaping") {
    constexpr MessageSpec kQuoted{"ux.test_quoted", "Say \"hi\"\\ now", {}};
    const std::array<const MessageSpec*, 2> registry{&kQuoted, &kQuoted};
    const Result<std::string> rendered = MessageCatalogExport(registry).render(std::nullopt);
    REQUIRE(rendered);
    CHECK(*rendered == "{\n  \"@@locale\": \"en\",\n  \"ux.test_quoted\": \"Say \\\"hi\\\"\\\\ now\"\n}\n");
}

TEST_CASE("render and check refuse a malformed catalog") {
    const MessageCatalogExport exporter(kRegistry);
    for (const std::string_view bad : {"{", "[]", "\"text\"", "{\"a\": }"}) {
        INFO(std::string(bad));
        const Result<std::string> rendered = exporter.render(bad);
        REQUIRE_FALSE(rendered);
        CHECK(rendered.error().id == "ux.malformed_catalog");
        CHECK(rendered.error().detail.has_value());
        const Result<std::vector<CatalogIssue>> checked = exporter.check(bad);
        REQUIRE_FALSE(checked);
        CHECK(checked.error().id == "ux.malformed_catalog");
    }
}

TEST_CASE("check reports missing and orphaned ids") {
    const std::vector<CatalogIssue> issues =
        check(R"({"@@locale": "en", "ux.test_hello": "Hi {name}", "ux.gone": "x", "@ux.gone": {}, "gui.kept": "y"})");
    REQUIRE(issues.size() == 3);
    CHECK(has(issues, CatalogIssueKind::MissingId, "net.test_ports"));
    CHECK(has(issues, CatalogIssueKind::MissingId, "play.test_plain"));
    CHECK(has(issues, CatalogIssueKind::OrphanedId, "ux.gone"));
    CHECK(issues[0].id == "net.test_ports");
}

TEST_CASE("check compares placeholders by name") {
    SECTION("a missing and an extra placeholder") {
        const std::vector<CatalogIssue> issues =
            check(R"({"ux.test_hello": "Hello {who}", "net.test_ports": "Ports {last} to {first}",
                      "play.test_plain": "Nothing"})");
        REQUIRE(issues.size() == 1);
        CHECK(issues[0].kind == CatalogIssueKind::PlaceholderMismatch);
        CHECK(issues[0].id == "ux.test_hello");
        CHECK(issues[0].detail == "expected {name}, found {who}");
    }
    SECTION("plural and select arguments count as placeholders") {
        const std::vector<CatalogIssue> issues =
            check(R"({"ux.test_hello": "{name, select, other {Hi}}", "net.test_ports":
                      "{first, plural, one {# port} other {# ports}} until {last, number}",
                      "play.test_plain": "Nothing"})");
        CHECK(issues.empty());
    }
    SECTION("UI-owned keys are not compared") {
        CHECK(check_template("gui.any", "Hello {anything}").empty());
    }
}

TEST_CASE("check compares an id declared twice against each declaration") {
    static constexpr std::array<ArgSpec, 1> kOtherArgs{ArgSpec{"other"}};
    static constexpr MessageSpec kWindows{"platform.test_twice", "On {name}", kNameArgs};
    static constexpr MessageSpec kLinux{"platform.test_twice", "On {other}", kOtherArgs};
    const std::array<const MessageSpec*, 2> registry{&kWindows, &kLinux};
    const Result<std::vector<CatalogIssue>> issues =
        MessageCatalogExport(registry).check(R"({"platform.test_twice": "On {name}"})");
    REQUIRE(issues);
    REQUIRE(issues->size() == 2);
    CHECK((*issues)[0].kind == CatalogIssueKind::PlaceholderMismatch);
    CHECK((*issues)[0].detail == "expected {other}, found {name}");
    CHECK((*issues)[1].kind == CatalogIssueKind::ConflictingDeclarations);
}

TEST_CASE("check allows only English one/other plurals") {
    CHECK(check_template("gui.ok", "{n, plural, one {# item} other {# items}}").empty());
    CHECK(check_template("gui.ok", "{n, plural, other {# items}}").empty());

    const std::vector<CatalogIssue> few = check_template("gui.bad", "{n, plural, one {a} few {b} other {c}}");
    REQUIRE(few.size() == 1);
    CHECK(few[0].kind == CatalogIssueKind::UnsupportedPlural);
    CHECK(few[0].detail == "n: selector few");

    CHECK(has(check_template("gui.bad", "{n, plural, =0 {none} other {some}}"), CatalogIssueKind::UnsupportedPlural,
              "gui.bad"));
    CHECK(has(check_template("gui.bad", "{n, plural, offset:1 other {#}}"), CatalogIssueKind::UnsupportedPlural,
              "gui.bad"));
    CHECK(has(check_template("gui.bad", "{n, selectordinal, one {#st} other {#th}}"),
              CatalogIssueKind::UnsupportedPlural, "gui.bad"));
    CHECK(has(check_template("gui.bad", "{n, choice, 0#none|1#one}"), CatalogIssueKind::UnsupportedPlural,
              "gui.bad"));
}

TEST_CASE("check reports malformed templates") {
    for (const std::string_view bad :
         {"Hello {name", "Hello name}", "{}", "{ , number}", "{n, wat}", "{n, plural, one {a}}",
          "{n, plural, other {a} other {b}}", "{n, plural, lots {a} other {b}}", "{n, plural, other a}",
          "{n, select, male {he}}", "'{unterminated", "{n, number, integer"}) {
        INFO(std::string(bad));
        const std::vector<CatalogIssue> issues = check_template("gui.bad", bad);
        REQUIRE(issues.size() == 1);
        CHECK(issues[0].kind == CatalogIssueKind::MalformedTemplate);
        CHECK_FALSE(issues[0].detail.empty());
    }
    const std::vector<CatalogIssue> not_string = check(R"({"ux.test_hello": 5, "net.test_ports": "Ports {first} to {last}",
                                                          "play.test_plain": "x"})");
    REQUIRE(not_string.size() == 1);
    CHECK(not_string[0].kind == CatalogIssueKind::MalformedTemplate);
}

TEST_CASE("check follows ICU's apostrophe rules") {
    CHECK(check_template("gui.ok", "The launcher's data").empty());
    CHECK(check_template("gui.ok", "Use '{braces}' literally").empty());
    CHECK(check_template("gui.ok", "Two '''' apostrophes").empty());
    CHECK(check_template("gui.ok", "{n, plural, other {'#' is literal, # is not}}").empty());
    CHECK(check_template("gui.ok", "{n, number, ::currency/EUR}").empty());
}

TEST_CASE("check quotes '#' only directly inside a plural and '|' only inside a choice") {
    CHECK(check_template("gui.ok", "{n, plural, other {{g, select, other {it'#s}}}}").empty());
    CHECK(check_template("gui.ok", "Either'|or").empty());
    CHECK(check_template("gui.ok", "{n, number, '#'0}").empty());
    CHECK(check_template("gui.ok", "{n, number, 'it''s'}").empty());
    const std::vector<CatalogIssue> choice = check_template("gui.bad", "{n, choice, 0#none'|'|1#one}");
    REQUIRE(choice.size() == 1);
    CHECK(choice[0].kind == CatalogIssueKind::UnsupportedPlural);
    CHECK(has(check_template("gui.bad", "{n, number, 'open}"), CatalogIssueKind::MalformedTemplate, "gui.bad"));
}

TEST_CASE("check reports an empty template") {
    for (const std::string_view empty : {"", " ", "\\n\\t"}) {
        INFO(std::string(empty));
        const std::vector<CatalogIssue> issues = check_template("gui.empty", empty);
        REQUIRE(issues.size() == 1);
        CHECK(issues[0].kind == CatalogIssueKind::MalformedTemplate);
        CHECK(issues[0].detail == "the template is empty");
    }
}

TEST_CASE("check reports an id declared twice with different English") {
    static constexpr MessageSpec kWindows{"platform.test_twice", "On {name}", kNameArgs};
    static constexpr MessageSpec kLinux{"platform.test_twice", "Running on {name}", kNameArgs};
    static constexpr MessageSpec kMacos{"platform.test_twice", "On {name}", kNameArgs};
    const std::array<const MessageSpec*, 4> registry{&kWindows, &kHello, &kMacos, &kLinux};
    const Result<std::vector<CatalogIssue>> issues = MessageCatalogExport(registry).check(
        R"({"platform.test_twice": "On {name}", "ux.test_hello": "Hello {name}"})");
    REQUIRE(issues);
    REQUIRE(issues->size() == 1);
    CHECK((*issues)[0].kind == CatalogIssueKind::ConflictingDeclarations);
    CHECK((*issues)[0].id == "platform.test_twice");

    const std::array<const MessageSpec*, 2> alike{&kWindows, &kMacos};
    const Result<std::vector<CatalogIssue>> clean =
        MessageCatalogExport(alike).check(R"({"platform.test_twice": "On {name}"})");
    REQUIRE(clean);
    CHECK(clean->empty());
}

TEST_CASE("check refuses templates nested too deep") {
    std::string deep;
    for (int i = 0; i < 20; ++i) deep += "{n, select, other {";
    for (int i = 0; i < 20; ++i) deep += "}}";
    CHECK(has(check_template("gui.deep", deep), CatalogIssueKind::MalformedTemplate, "gui.deep"));
}

TEST_CASE("ux's own registry renders into a catalog that passes check") {
    const MessageCatalogExport exporter(message_registry());
    const Result<std::string> rendered = exporter.render(std::nullopt);
    REQUIRE(rendered);
    const Result<std::vector<CatalogIssue>> issues = exporter.check(*rendered);
    REQUIRE(issues);
    for (const CatalogIssue& issue : *issues) FAIL_CHECK(issue.id + ": " + issue.detail);
}

TEST_CASE("MessageCatalog::english_from_registry serves registry templates") {
    const MessageCatalog catalog = MessageCatalog::english_from_registry();
    CHECK(catalog.language() == LanguageTag::english());
    CHECK(catalog.text("ux.choice_keep_unlisted") == "Keep unlisted");
    CHECK(catalog.text("ux.help_port_forwarding_port").has_value());
    CHECK_FALSE(catalog.text("ux.no_such_message"));
    CHECK_FALSE(catalog.text(""));
}
