// Included alone first: enums of other packages must be persisted enums with nothing else in scope.
#include "reboot/storage/json_values.hpp"

static_assert(reboot::storage::PersistedEnum<reboot::contracts::ipc::ClientKind>);
static_assert(reboot::storage::PersistedEnum<reboot::contracts::ipc::EngineOrigin>);
static_assert(reboot::storage::PersistedEnum<reboot::contracts::backend::AccountRole>);
static_assert(reboot::storage::PersistedEnum<reboot::ports::IntegrationKind>);

#include <chrono>
#include <concepts>
#include <span>
#include <string>

#include <boost/json/object.hpp>
#include <boost/json/value.hpp>
#include <catch2/catch_test_macros.hpp>

#include "reboot/foundation/native_path.hpp"
#include "reboot/foundation/net_types.hpp"
#include "reboot/storage/console_key.hpp"
#include "reboot/storage/load_report.hpp"
#include "reboot/testing/fake_platform_paths.hpp"

using namespace reboot;
using namespace reboot::storage;

namespace json = boost::json;

TEST_CASE("enums are stored by name", "[storage][json]") {
    CHECK(enum_to_json(contracts::ipc::ClientKind::MacGui) == json::value("mac_gui"));
    const Result<contracts::ipc::ClientKind> cli = enum_from_json<contracts::ipc::ClientKind>(json::value("cli"));
    REQUIRE(cli);
    CHECK(*cli == contracts::ipc::ClientKind::Cli);

    const Result<contracts::ipc::ClientKind> unknown = enum_from_json<contracts::ipc::ClientKind>(json::value(4));
    REQUIRE_FALSE(unknown);
    CHECK(unknown.error().id == "storage.wrong_type");
    const Result<contracts::ipc::ClientKind> renamed = enum_from_json<contracts::ipc::ClientKind>(json::value("gui"));
    REQUIRE_FALSE(renamed);
    CHECK(renamed.error().id == "storage.unknown_name");
}

TEST_CASE("paths and times round-trip", "[storage][json]") {
    for (const NativePath& path : {testing::default_fake_root() / "builds" / "8.51",
                                   testing::default_fake_root() / std::u8string(u8"Bj\u00f6rk \u30d3\u30eb\u30c9")}) {
        const json::value stored = path_to_json(path);
        CHECK(stored.is_string());
        const Result<NativePath> decoded = path_from_json(stored);
        REQUIRE(decoded);
        CHECK(*decoded == path);
    }

    const auto time = std::chrono::system_clock::time_point{std::chrono::microseconds{1791417509000001}};
    const Result<std::chrono::system_clock::time_point> read = time_from_json(time_to_json(time));
    REQUIRE(read);
    CHECK(*read == time);
}

TEST_CASE("numbers out of range are refused with storage.out_of_range", "[storage][json]") {
    const Result<u64> negative = u64_from_json(json::value(-1));
    REQUIRE_FALSE(negative);
    CHECK(negative.error().id == "storage.out_of_range");

    const Result<Port> zero = port_from_json(json::value(0));
    REQUIRE_FALSE(zero);
    CHECK(zero.error().id == "storage.out_of_range");

    const Result<u64> fraction = u64_from_json(json::value(1.5));
    REQUIRE_FALSE(fraction);
    CHECK(fraction.error().id == "storage.out_of_range");
}

TEST_CASE("console keys are Unreal key names", "[storage][json]") {
    CHECK(ConsoleKey::parse("Tilde"));
    CHECK(ConsoleKey::parse("F8"));
    CHECK_FALSE(ConsoleKey::parse("f8"));
    CHECK_FALSE(ConsoleKey::parse("Minus"));
}

TEST_CASE("the combined mode is the most restricted one", "[storage][json]") {
    LoadReport read_only;
    read_only.mode = StorageMode::ReadOnly;
    LoadReport in_memory;
    in_memory.mode = StorageMode::InMemory;
    const LoadReport reports[] = {LoadReport{}, read_only, in_memory};
    CHECK(combined_mode(reports) == StorageMode::InMemory);
    CHECK(combined_mode(std::span(reports, 2)) == StorageMode::ReadOnly);
}

TEST_CASE("a path that is not valid Unicode is stored as its native bytes", "[storage][json]") {
    NativePath path = testing::default_fake_root();
    if constexpr (std::same_as<NativePath::value_type, wchar_t>)
        path /= std::wstring{L'b', L'a', L'd', wchar_t(0xD800)};
    else
        path /=std::string("bad\xff");

    const json::value stored = path_to_json(path);
    REQUIRE(stored.is_object());
    CHECK(stored.as_object().contains("native"));
    const Result<NativePath> decoded = path_from_json(stored);
    REQUIRE(decoded);
    CHECK(decoded->native() == path.native());

    for (const json::value& bad : {json::value(json::object{{"native", "!!!!"}}), json::value(json::object{}),
                                   json::value("")}) {
        const Result<NativePath> refused = path_from_json(bad);
        REQUIRE_FALSE(refused);
        CHECK(refused.error().id == "storage.invalid_path");
    }
}

TEST_CASE("a stored path with an embedded NUL is refused", "[storage][json]") {
    constexpr char kTruncated[] = "/opt/game/evil.exe\0.dll";
    const Result<NativePath> text = path_from_json(json::value(json::string_view(kTruncated, sizeof kTruncated - 1)));
    REQUIRE_FALSE(text);
    CHECK(text.error().id == "storage.invalid_path");

    // The native form of "a\0b": UTF-16LE on Windows, bytes elsewhere.
    const char* native = std::same_as<NativePath::value_type, wchar_t> ? "YQAAAGIA" : "YQBi";
    const Result<NativePath> encoded = path_from_json(json::object{{"native", native}});
    REQUIRE_FALSE(encoded);
    CHECK(encoded.error().id == "storage.invalid_path");
}
