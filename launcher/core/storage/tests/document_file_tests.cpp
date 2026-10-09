#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <boost/json/object.hpp>
#include <boost/json/parse.hpp>
#include <catch2/catch_test_macros.hpp>

#include "reboot/foundation/cancel.hpp"
#include "reboot/foundation/clock.hpp"
#include "reboot/foundation/diag.hpp"
#include "reboot/foundation/executor.hpp"
#include "reboot/storage/document_store.hpp"
#include "reboot/storage/settings_document.hpp"
#include "reboot/testing/in_memory_file_system.hpp"
#include "test_support.hpp"

using namespace reboot;
using namespace reboot::storage;

namespace {

namespace json = boost::json;

// Schema 2 renamed "old_name" to "name".
struct ProbeDocument {
    static constexpr std::string_view kName = "probe";
    static constexpr u32 kSchema = 2;

    std::string name;

    static ProbeDocument read(const json::object& values, std::vector<ValueIssue>&) {
        ProbeDocument document;
        if (const json::value* name = values.if_contains("name"); name != nullptr && name->is_string())
            document.name = std::string(name->get_string());
        return document;
    }
    json::object write() const {
        json::object out;
        out.emplace("name", name);
        return out;
    }
    static Result<json::object> upgrade(json::object values, u32 from_schema) {
        if (from_schema == 1 && values.contains("old_name")) {
            // Copied out first: inserting "name" may move the member the reference points at.
            json::value old_name = values["old_name"];
            values.erase("old_name");
            values["name"] = std::move(old_name);
        }
        return values;
    }
};

static_assert(Document<ProbeDocument>);

// A schema 2 whose upgrade from 1 is impossible.
struct UnupgradableDocument : ProbeDocument {
    static UnupgradableDocument read(const json::object& values, std::vector<ValueIssue>& issues) {
        UnupgradableDocument document;
        static_cast<ProbeDocument&>(document) = ProbeDocument::read(values, issues);
        return document;
    }
    static Result<json::object> upgrade(json::object, u32) {
        return make_diag(ErrorDomain::Storage, MessageId{"storage.invalid_value"}).fail();
    }
};

static_assert(Document<UnupgradableDocument>);

struct Fixture {
    Fixture() {
        test::set_test_time(clock);
        fs.make_dir(dir);
    }

    template <class D>
    Result<void> flush(DocumentStore<D>& store) {
        std::optional<Result<void>> result;
        store.flush({}, [&result](Result<void> flushed) { result = std::move(flushed); });
        strand.run_until([&result] { return result.has_value(); });
        return *result;
    }

    test::WorkerStrand strand;
    WorkerPool workers{1};
    ManualClock clock;
    testing::InMemoryFileSystem fs;
    NativePath dir = testing::default_fake_root() / "config";
    NativePath settings_path = dir / "settings.json";
    NativePath probe_path = dir / "probe.json";
};

[[nodiscard]] NativePath with_suffix(NativePath path, std::string_view suffix) {
    path += suffix;
    return path;
}

[[nodiscard]] Diagnostic disk_error() {
    return make_diag(ErrorDomain::Storage, MessageId{"storage.write_failed"}).build();
}

[[nodiscard]] json::object envelope_on_disk(const testing::InMemoryFileSystem& fs, const NativePath& path) {
    const std::optional<std::string> text = fs.text(path);
    REQUIRE(text);
    return json::parse(*text).as_object();
}

}  // namespace

TEST_CASE("a changed document is written as a {schema, revision, values} envelope", "[storage][document]") {
    Fixture f;
    DocumentStore<SettingsDocument> store(f.fs, f.workers, f.strand, f.clock, f.settings_path);
    const LoadReport report = store.load();
    CHECK(report.source == LoadSource::Fresh);
    CHECK(report.mode == StorageMode::ReadWrite);

    REQUIRE(store.update([](SettingsDocument& document) { document.values.ui.theme = Theme::Dark; }) == 1u);
    REQUIRE(f.flush(store));
    CHECK(f.fs.text(f.settings_path) == test::golden_text("settings_envelope.json"));
}

TEST_CASE("an unreadable primary falls back to its .bak", "[storage][document]") {
    Fixture f;
    f.fs.write_text(f.settings_path, "{ \"schema\": 1, \"revision\"");
    f.fs.write_text(with_suffix(f.settings_path, ".bak"), test::golden_text("settings_envelope.json"));
    DocumentStore<SettingsDocument> store(f.fs, f.workers, f.strand, f.clock, f.settings_path);

    const LoadReport report = store.load();
    CHECK(report.source == LoadSource::Backup);
    REQUIRE(report.reason);
    CHECK(report.reason->id == "storage.restored_from_backup");
    CHECK(store.get().values.ui.theme == Theme::Dark);
    CHECK(store.revision() == 1u);
}

TEST_CASE("a primary and .bak that both fail are quarantined under a UTC name", "[storage][document]") {
    Fixture f;
    f.fs.write_text(f.settings_path, "not json");
    DocumentStore<SettingsDocument> store(f.fs, f.workers, f.strand, f.clock, f.settings_path);

    const LoadReport report = store.load();
    const NativePath quarantine = with_suffix(f.settings_path, ".corrupt-20261007T235829Z");
    CHECK(report.source == LoadSource::Defaults);
    CHECK(report.mode == StorageMode::ReadWrite);
    REQUIRE(report.quarantined_to);
    CHECK(*report.quarantined_to == quarantine);
    CHECK(f.fs.text(quarantine) == "not json");
    CHECK(store.get().values == SettingsValues{});

    // The unreadable primary is not kept as the .bak.
    REQUIRE(store.update([](SettingsDocument& document) { document.values.ui.theme = Theme::Dark; }));
    REQUIRE(f.flush(store));
    CHECK_FALSE(f.fs.exists(with_suffix(f.settings_path, ".bak")));
    CHECK(f.fs.text(f.settings_path) == test::golden_text("settings_envelope.json"));
}

TEST_CASE("a hand edit is reloaded and the engine's unwritten members are laid over it", "[storage][document]") {
    Fixture f;
    f.fs.write_text(f.settings_path, test::golden_text("hand_edit_before.json"));
    DocumentStore<SettingsDocument> store(f.fs, f.workers, f.strand, f.clock, f.settings_path);
    REQUIRE(store.load().source == LoadSource::Primary);
    bool reloaded = false;
    store.set_on_reload([&reloaded](const SettingsDocument&, std::span<const ValueIssue>) { reloaded = true; });

    f.fs.write_text(f.settings_path, test::golden_text("hand_edit_edited.json"));
    REQUIRE(store.update([](SettingsDocument& document) { document.values.ui.theme = Theme::Light; }));
    REQUIRE(f.flush(store));

    CHECK(reloaded);
    CHECK(store.get().values.ui.language == "de");
    CHECK(store.get().values.ui.theme == Theme::Light);
    CHECK(store.get().unknown.contains("future.key"));
    CHECK(store.revision() == 6u);
    CHECK(f.fs.text(f.settings_path) == test::golden_text("hand_edit_after.json"));
}

TEST_CASE("an older schema is backed up as <name>.v<N>.json, then upgraded", "[storage][document]") {
    Fixture f;
    const std::string original = test::golden_text("probe_v1.json");
    f.fs.write_text(f.probe_path, original);
    DocumentStore<ProbeDocument> store(f.fs, f.workers, f.strand, f.clock, f.probe_path);

    const LoadReport report = store.load();
    CHECK(report.schema_on_disk == 1u);
    CHECK(report.mode == StorageMode::ReadWrite);
    REQUIRE(report.schema_backup);
    CHECK(*report.schema_backup == f.dir / "probe.v1.json");
    CHECK(f.fs.text(f.dir / "probe.v1.json") == original);
    CHECK(store.get().name == "kept");
}

TEST_CASE("a newer schema opens read-only and refuses changes", "[storage][document]") {
    Fixture f;
    f.fs.write_text(f.probe_path, R"({"schema": 3, "revision": 1, "values": {"name": "future"}})");
    DocumentStore<ProbeDocument> store(f.fs, f.workers, f.strand, f.clock, f.probe_path);

    const LoadReport report = store.load();
    CHECK(report.mode == StorageMode::ReadOnly);
    CHECK(store.get().name == "future");
    const Result<u64> refused = store.update([](ProbeDocument& document) { document.name = "mine"; });
    REQUIRE_FALSE(refused);
    CHECK(refused.error().id == "storage.read_only");
    CHECK(refused.error().kind == ErrorKind::Conflict);
}

TEST_CASE("a failed write switches the store to InMemory until a write succeeds", "[storage][document]") {
    Fixture f;
    DocumentStore<SettingsDocument> store(f.fs, f.workers, f.strand, f.clock, f.settings_path);
    REQUIRE(store.load().mode == StorageMode::ReadWrite);
    std::vector<StorageModeChanged> changes;
    store.set_on_mode_changed([&changes](const StorageModeChanged& change) { changes.push_back(change); });

    f.fs.faults().fail_next(testing::FsOperation::AtomicReplace,
                            make_diag(ErrorDomain::Storage, MessageId{"storage.write_failed"}).build());
    REQUIRE(store.update([](SettingsDocument& document) { document.values.ui.theme = Theme::Dark; }));
    const Result<void> failed = f.flush(store);
    REQUIRE_FALSE(failed);
    CHECK(failed.error().id == "storage.write_failed");
    CHECK(store.mode() == StorageMode::InMemory);
    REQUIRE(changes.size() == 1);
    CHECK(changes[0].document == "settings");
    CHECK(changes[0].mode == StorageMode::InMemory);
    CHECK(changes[0].reason.has_value());

    REQUIRE(f.flush(store));
    CHECK(store.mode() == StorageMode::ReadWrite);
    REQUIRE(changes.size() == 2);
    CHECK(changes[1].mode == StorageMode::ReadWrite);
    CHECK(f.fs.text(f.settings_path) == test::golden_text("settings_envelope.json"));
}

TEST_CASE("a memory-only store never touches disk", "[storage][document]") {
    Fixture f;
    DocumentStore<SettingsDocument> store(f.fs, f.workers, f.strand, f.clock, f.settings_path);
    const LoadReport report =
        store.load_memory_only(make_diag(ErrorDomain::Storage, MessageId{"storage.root_not_writable"}).build());
    CHECK(report.mode == StorageMode::InMemory);
    REQUIRE(store.update([](SettingsDocument& document) { document.values.ui.theme = Theme::Dark; }));
    REQUIRE(f.flush(store));
    CHECK_FALSE(f.fs.exists(f.settings_path));
}

TEST_CASE("an upgraded document is written at the current schema on the next flush", "[storage][document]") {
    Fixture f;
    f.fs.write_text(f.probe_path, test::golden_text("probe_v1.json"));
    DocumentStore<ProbeDocument> store(f.fs, f.workers, f.strand, f.clock, f.probe_path);
    REQUIRE(store.load().schema_backup);

    REQUIRE(f.flush(store));
    const json::object written = envelope_on_disk(f.fs, f.probe_path);
    CHECK(written.at("schema") == json::value(2));
    CHECK(written.at("revision") == json::value(2));
    CHECK(written.at("values").as_object() == json::object{{"name", "kept"}});
}

TEST_CASE("an upgrade that fails opens the document read-only", "[storage][document]") {
    Fixture f;
    const std::string original = test::golden_text("probe_v1.json");
    f.fs.write_text(f.probe_path, original);
    DocumentStore<UnupgradableDocument> store(f.fs, f.workers, f.strand, f.clock, f.probe_path);

    const LoadReport report = store.load();
    CHECK(report.mode == StorageMode::ReadOnly);
    REQUIRE(report.reason);
    CHECK(report.reason->id == "storage.upgrade_failed");
    CHECK(report.schema_backup == f.dir / "probe.v1.json");
    const Result<u64> refused = store.update([](UnupgradableDocument& document) { document.name = "mine"; });
    REQUIRE_FALSE(refused);
    CHECK(refused.error().id == "storage.upgrade_failed");
    CHECK(refused.error().severity == Severity::Error);
    REQUIRE(f.flush(store));
    CHECK(f.fs.text(f.probe_path) == original);
}

TEST_CASE("an older schema that cannot be backed up opens read-only", "[storage][document]") {
    Fixture f;
    f.fs.write_text(f.probe_path, test::golden_text("probe_v1.json"));
    f.fs.faults().fail_next(testing::FsOperation::AtomicReplace, disk_error());
    DocumentStore<ProbeDocument> store(f.fs, f.workers, f.strand, f.clock, f.probe_path);

    const LoadReport report = store.load();
    CHECK(report.mode == StorageMode::ReadOnly);
    REQUIRE(report.reason);
    CHECK(report.reason->id == "storage.schema_backup_failed");
    CHECK_FALSE(report.schema_backup);
    CHECK_FALSE(f.fs.exists(f.dir / "probe.v1.json"));
    const Result<u64> refused = store.update([](ProbeDocument& document) { document.name = "mine"; });
    REQUIRE_FALSE(refused);
    CHECK(refused.error().id == "storage.schema_backup_failed");
}

TEST_CASE("a primary that cannot be read for another reason loads memory-only", "[storage][document]") {
    Fixture f;
    const std::string original = test::golden_text("settings_envelope.json");
    f.fs.write_text(f.settings_path, original);
    f.fs.faults().fail_next(testing::FsOperation::ReadAll, disk_error());
    DocumentStore<SettingsDocument> store(f.fs, f.workers, f.strand, f.clock, f.settings_path);

    const LoadReport report = store.load();
    CHECK(report.mode == StorageMode::InMemory);
    REQUIRE(report.reason);
    CHECK(report.reason->id == "storage.memory_only");
    REQUIRE(store.update([](SettingsDocument& document) { document.values.ui.theme = Theme::Light; }));
    REQUIRE(f.flush(store));
    CHECK(f.fs.text(f.settings_path) == original);
}

TEST_CASE("an unreadable primary that cannot be quarantined is never overwritten", "[storage][document]") {
    Fixture f;
    f.fs.write_text(f.settings_path, "not json");
    f.fs.faults().fail_next(testing::FsOperation::AtomicReplace, disk_error());
    DocumentStore<SettingsDocument> store(f.fs, f.workers, f.strand, f.clock, f.settings_path);

    const LoadReport report = store.load();
    CHECK(report.mode == StorageMode::InMemory);
    CHECK(report.source == LoadSource::Defaults);
    REQUIRE(report.reason);
    CHECK(report.reason->id == "storage.quarantine_failed");
    REQUIRE(store.update([](SettingsDocument& document) { document.values.ui.theme = Theme::Dark; }));
    REQUIRE(f.flush(store));
    CHECK(f.fs.text(f.settings_path) == "not json");
}

TEST_CASE("a cancelled flush ends only the wait and the write still lands", "[storage][document]") {
    Fixture f;
    DocumentStore<SettingsDocument> store(f.fs, f.workers, f.strand, f.clock, f.settings_path);
    REQUIRE(store.load().source == LoadSource::Fresh);
    test::WorkerGate gate(f.workers, f.strand);
    REQUIRE(store.update([](SettingsDocument& document) { document.values.ui.theme = Theme::Dark; }) == 1u);

    CancelSource cancel;
    std::optional<Result<void>> waited;
    store.flush(cancel.token(), [&waited](Result<void> flushed) { waited = std::move(flushed); });
    cancel.cancel(CancelReason::User);
    f.strand.run_until([&waited] { return waited.has_value(); });
    REQUIRE_FALSE(*waited);
    CHECK(waited->error().id == "storage.cancelled");
    CHECK_FALSE(f.fs.exists(f.settings_path));

    gate.release();
    REQUIRE(f.flush(store));
    CHECK(f.fs.text(f.settings_path) == test::golden_text("settings_envelope.json"));
}

TEST_CASE("changes made while a write is in flight go out in the next write", "[storage][document]") {
    Fixture f;
    DocumentStore<SettingsDocument> store(f.fs, f.workers, f.strand, f.clock, f.settings_path);
    REQUIRE(store.load().source == LoadSource::Fresh);
    test::WorkerGate gate(f.workers, f.strand);
    REQUIRE(store.update([](SettingsDocument& document) { document.values.ui.theme = Theme::Dark; }) == 1u);
    REQUIRE(store.update([](SettingsDocument& document) { document.values.ui.language = "de"; }) == 2u);

    gate.release();
    REQUIRE(f.flush(store));
    const json::object written = envelope_on_disk(f.fs, f.settings_path);
    CHECK(written.at("revision") == json::value(2));
    CHECK(written.at("values").at("ui.theme") == json::value("dark"));
    CHECK(written.at("values").at("ui.language") == json::value("de"));
    // The first write left its file as the backup of the second.
    CHECK(f.fs.exists(with_suffix(f.settings_path, ".bak")));
}

TEST_CASE("refresh reloads a hand edit made while nothing was pending", "[storage][document]") {
    Fixture f;
    f.fs.write_text(f.settings_path, test::golden_text("hand_edit_before.json"));
    DocumentStore<SettingsDocument> store(f.fs, f.workers, f.strand, f.clock, f.settings_path);
    REQUIRE(store.load().source == LoadSource::Primary);
    std::optional<std::string> reloaded_language;
    store.set_on_reload([&reloaded_language](const SettingsDocument& document, std::span<const ValueIssue>) {
        reloaded_language = document.values.ui.language;
    });

    const std::string edited = test::golden_text("hand_edit_edited.json");
    f.fs.write_text(f.settings_path, edited);
    store.refresh();
    REQUIRE(f.flush(store));
    CHECK(reloaded_language == "de");
    CHECK(store.revision() == 5u);
    // Nothing of the engine's was pending, so the edit stays as the user wrote it.
    CHECK(f.fs.text(f.settings_path) == edited);
}

TEST_CASE("a change made while a hand edit is reloaded is written after the merge", "[storage][document]") {
    Fixture f;
    f.fs.write_text(f.settings_path, test::golden_text("hand_edit_before.json"));
    DocumentStore<SettingsDocument> store(f.fs, f.workers, f.strand, f.clock, f.settings_path);
    REQUIRE(store.load().source == LoadSource::Primary);
    store.set_on_reload([&store](const SettingsDocument& document, std::span<const ValueIssue>) {
        if (document.values.ui.theme == Theme::System)
            REQUIRE(store.update([](SettingsDocument& next) { next.values.ui.theme = Theme::Light; }));
    });

    f.fs.write_text(f.settings_path, test::golden_text("hand_edit_edited.json"));
    store.refresh();
    REQUIRE(f.flush(store));
    CHECK(store.get().values.ui.language == "de");
    CHECK(store.get().values.ui.theme == Theme::Light);
    CHECK(f.fs.text(f.settings_path) == test::golden_text("hand_edit_after.json"));
}

TEST_CASE("a refresh that cannot look at the file leaves the mode alone", "[storage][document]") {
    Fixture f;
    DocumentStore<SettingsDocument> store(f.fs, f.workers, f.strand, f.clock, f.settings_path);
    REQUIRE(store.load().mode == StorageMode::ReadWrite);
    std::vector<StorageModeChanged> changes;
    store.set_on_mode_changed([&changes](const StorageModeChanged& change) { changes.push_back(change); });

    f.fs.faults().fail_next(testing::FsOperation::Revision, disk_error());
    store.refresh();
    REQUIRE(f.flush(store));
    CHECK(store.mode() == StorageMode::ReadWrite);
    CHECK(changes.empty());
}

TEST_CASE("a missing primary is restored from its .bak", "[storage][document]") {
    Fixture f;
    const NativePath backup = with_suffix(f.settings_path, ".bak");
    f.fs.write_text(backup, test::golden_text("settings_envelope.json"));
    DocumentStore<SettingsDocument> store(f.fs, f.workers, f.strand, f.clock, f.settings_path);

    const LoadReport report = store.load();
    CHECK(report.source == LoadSource::Backup);
    CHECK(report.mode == StorageMode::ReadWrite);
    REQUIRE(report.reason);
    CHECK(report.reason->id == "storage.restored_from_backup");
    CHECK(store.get().values.ui.theme == Theme::Dark);
    CHECK(store.revision() == 1u);

    REQUIRE(store.update([](SettingsDocument& document) { document.values.ui.theme = Theme::Light; }) == 2u);
    REQUIRE(f.flush(store));
    CHECK(f.fs.text(backup) == test::golden_text("settings_envelope.json"));
    CHECK(envelope_on_disk(f.fs, f.settings_path).at("values").at("ui.theme") == "light");
}

TEST_CASE("a document saved with a UTF-8 byte order mark is read", "[storage][document]") {
    Fixture f;
    f.fs.write_text(f.settings_path, "\xEF\xBB\xBF" + test::golden_text("settings_envelope.json"));
    DocumentStore<SettingsDocument> store(f.fs, f.workers, f.strand, f.clock, f.settings_path);

    const LoadReport report = store.load();
    CHECK(report.source == LoadSource::Primary);
    CHECK_FALSE(report.reason);
    CHECK(store.get().values.ui.theme == Theme::Dark);
}

TEST_CASE("a write that lands but cannot be stat'ed afterwards is not a failed write", "[storage][document]") {
    Fixture f;
    DocumentStore<SettingsDocument> store(f.fs, f.workers, f.strand, f.clock, f.settings_path);
    REQUIRE(store.load().source == LoadSource::Fresh);
    std::vector<StorageModeChanged> changes;
    store.set_on_mode_changed([&changes](const StorageModeChanged& change) { changes.push_back(change); });

    // Both stats of the first write fail; the one before it reads as a file not created yet.
    f.fs.faults().fail_next(testing::FsOperation::Revision,
                            make_diag(ErrorDomain::Storage, MessageId{"storage.write_failed"})
                                .kind(ErrorKind::NotFound)
                                .build(),
                            2);
    REQUIRE(store.update([](SettingsDocument& document) { document.values.ui.theme = Theme::Dark; }));
    REQUIRE(f.flush(store));
    CHECK(store.mode() == StorageMode::ReadWrite);
    CHECK(changes.empty());
    CHECK(f.fs.text(f.settings_path) == test::golden_text("settings_envelope.json"));

    REQUIRE(store.update([](SettingsDocument& document) { document.values.ui.theme = Theme::Light; }));
    REQUIRE(f.flush(store));
    CHECK(store.get().values.ui.theme == Theme::Light);
    CHECK(envelope_on_disk(f.fs, f.settings_path).at("values").at("ui.theme") == "light");
}
