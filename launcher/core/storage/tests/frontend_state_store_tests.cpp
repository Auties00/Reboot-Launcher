#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "reboot/foundation/cancel.hpp"
#include "reboot/foundation/diag.hpp"
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

    Result<void> flush() {
        std::optional<Result<void>> result;
        store.flush({}, [&result](Result<void> done) { result = std::move(done); });
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
    CHECK_FALSE(ShellName::parse("con"));
    CHECK_FALSE(ShellName::parse("nul"));
    CHECK_FALSE(ShellName::parse("com1"));
    CHECK_FALSE(ShellName::parse("lpt9"));
    CHECK(ShellName::parse("console"));
    CHECK(ShellName::parse("com10"));
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

TEST_CASE("a cancelled put ends only the wait and the blob still lands", "[storage][frontend]") {
    Fixture f;
    test::WorkerGate gate(f.workers, f.strand);
    CancelSource cancel;
    std::optional<Result<void>> waited;
    f.store.put(f.winui, bytes("{}"), cancel.token(), [&waited](Result<void> done) { waited = std::move(done); });
    cancel.cancel(CancelReason::User);
    f.strand.run_until([&waited] { return waited.has_value(); });
    REQUIRE_FALSE(*waited);
    CHECK(waited->error().id == "storage.cancelled");
    CHECK_FALSE(f.fs.exists(f.dir / "winui.json"));

    gate.release();
    REQUIRE(f.flush());
    CHECK(f.fs.text(f.dir / "winui.json") == "{}");
}

TEST_CASE("a flush reports a write that failed", "[storage][frontend]") {
    Fixture f;
    f.fs.faults().fail_next(testing::FsOperation::AtomicReplace,
                            make_diag(ErrorDomain::Storage, MessageId{"storage.write_failed"}).build());
    std::optional<Result<void>> put;
    std::optional<Result<void>> flushed;
    f.store.put(f.winui, bytes("{}"), {}, [&put](Result<void> done) { put = std::move(done); });
    f.store.flush({}, [&flushed](Result<void> done) { flushed = std::move(done); });
    f.strand.run_until([&] { return put.has_value() && flushed.has_value(); });
    REQUIRE_FALSE(*put);
    CHECK(put->error().id == "storage.write_failed");
    REQUIRE_FALSE(*flushed);
    CHECK(flushed->error().id == "storage.write_failed");

    // The failure belonged to that drain; the next put and flush succeed.
    REQUIRE(f.put("{}"));
    REQUIRE(f.flush());
}

TEST_CASE("a window state that cannot be read is reported", "[storage][frontend]") {
    Fixture f;
    f.fs.write_text(f.dir / "winui.json", "{}");
    f.fs.faults().fail_next(testing::FsOperation::ReadAll,
                            make_diag(ErrorDomain::Storage, MessageId{"storage.memory_only"}).build());
    const Result<std::vector<u8>> failed = f.get();
    REQUIRE_FALSE(failed);
    CHECK(failed.error().id == "storage.memory_only");

    const Result<std::vector<u8>> stored = f.get();
    REQUIRE(stored);
    CHECK(*stored == bytes("{}"));
}

TEST_CASE("a put that lands while a read runs is what the read returns", "[storage][frontend]") {
    Fixture f;
    f.fs.write_text(f.dir / "winui.json", R"({"old": true})");
    test::WorkerGate gate(f.workers, f.strand);
    std::optional<Result<std::vector<u8>>> read;
    std::optional<Result<void>> written;
    f.store.get(f.winui, {}, [&read](Result<std::vector<u8>> done) { read = std::move(done); });
    f.store.put(f.winui, bytes(R"({"new": true})"), {}, [&written](Result<void> done) { written = std::move(done); });

    gate.release();
    f.strand.run_until([&] { return read.has_value() && written.has_value(); });
    REQUIRE(*read);
    CHECK(**read == bytes(R"({"new": true})"));
    REQUIRE(*written);
    CHECK(f.fs.text(f.dir / "winui.json") == R"({"new": true})");
}

TEST_CASE("after a failed put a get returns what the disk still holds", "[storage][frontend]") {
    Fixture f;
    REQUIRE(f.put(R"({"old": true})"));
    f.fs.faults().fail_next(testing::FsOperation::AtomicReplace,
                            make_diag(ErrorDomain::Storage, MessageId{"storage.write_failed"}).build());
    REQUIRE_FALSE(f.put(R"({"new": true})"));

    const Result<std::vector<u8>> stored = f.get();
    REQUIRE(stored);
    CHECK(*stored == bytes(R"({"old": true})"));
}
