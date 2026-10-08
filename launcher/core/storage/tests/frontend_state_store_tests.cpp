#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "reboot/foundation/executor.hpp"
#include "reboot/storage/frontend_state_store.hpp"
#include "reboot/storage/shell_name.hpp"
#include "reboot/testing/in_memory_file_system.hpp"
#include "test_support.hpp"

using namespace reboot;
using namespace reboot::storage;

namespace {

[[nodiscard]] std::vector<u8> bytes(std::string_view text) { return {text.begin(), text.end()}; }

struct Fixture {
    explicit Fixture(StorageMode mode = StorageMode::ReadWrite) : store(fs, workers, strand, dir, mode) {
        fs.make_dir(dir);
    }

    Result<void> put(std::string_view blob) {
        std::optional<Result<void>> result;
        store.put(winui, bytes(blob), {}, [&result](Result<void> done) { result = std::move(done); });
        strand.run_until([&result] { return result.has_value(); });
        return *result;
    }

    Result<std::vector<u8>> get() {
        std::optional<Result<std::vector<u8>>> result;
        store.get(winui, {}, [&result](Result<std::vector<u8>> done) { result = std::move(done); });
        strand.run_until([&result] { return result.has_value(); });
        return *result;
    }

    test::WorkerStrand strand;
    WorkerPool workers{1};
    testing::InMemoryFileSystem fs;
    NativePath dir = testing::default_fake_root() / "config" / "frontend";
    ShellName winui = *ShellName::parse("winui");
    FrontendStateStore store;
};

}  // namespace

TEST_CASE("shell names are lowercase, digits and dashes", "[storage][frontend]") {
    CHECK(ShellName::parse("swiftui"));
    CHECK(ShellName::parse("linux-gtk4"));
    CHECK_FALSE(ShellName::parse("WinUI"));
    CHECK_FALSE(ShellName::parse(""));
    CHECK_FALSE(ShellName::parse("../cli"));
}

TEST_CASE("a shell's window state is stored as JSON under config/frontend", "[storage][frontend]") {
    Fixture f;
    const Result<std::vector<u8>> empty = f.get();
    REQUIRE(empty);
    CHECK(empty->empty());

    constexpr std::string_view kBlob = R"({"width": 1164, "height": 864, "maximized": false})";
    REQUIRE(f.put(kBlob));
    CHECK(f.fs.text(f.dir / "winui.json") == std::string(kBlob));
    const Result<std::vector<u8>> stored = f.get();
    REQUIRE(stored);
    CHECK(*stored == bytes(kBlob));
}

TEST_CASE("window state that is not JSON or too large is refused", "[storage][frontend]") {
    Fixture f;
    const Result<void> not_json = f.put("width=1164");
    REQUIRE_FALSE(not_json);
    CHECK(not_json.error().id == "storage.frontend_state_not_json");

    const std::string large = "\"" + std::string(kFrontendStateMaxBytes, 'x') + "\"";
    const Result<void> too_large = f.put(large);
    REQUIRE_FALSE(too_large);
    CHECK(too_large.error().id == "storage.frontend_state_too_large");
    CHECK_FALSE(f.fs.exists(f.dir / "winui.json"));
}

TEST_CASE("in InMemory mode window state never reaches disk", "[storage][frontend]") {
    Fixture f(StorageMode::InMemory);
    REQUIRE(f.put("{}"));
    const Result<std::vector<u8>> stored = f.get();
    REQUIRE(stored);
    CHECK(*stored == bytes("{}"));
    CHECK_FALSE(f.fs.exists(f.dir / "winui.json"));
}
