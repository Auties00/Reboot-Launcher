#include "reboot/storage/document_file.hpp"

#include <algorithm>
#include <chrono>
#include <format>
#include <limits>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <boost/json/object.hpp>

#include "flush_waiters.hpp"
#include "json_text.hpp"
#include "messages.hpp"
#include "reboot/foundation/clock.hpp"
#include "reboot/foundation/executor.hpp"
#include "reboot/ports/file_system.hpp"
#include "reboot/storage/json_values.hpp"

namespace reboot::storage {

namespace json = boost::json;

namespace {

struct Envelope {
    u32 schema = 0;
    u64 revision = 0;
    json::object values;
};

[[nodiscard]] std::optional<Envelope> parse_envelope(std::span<const u8> bytes) {
    std::string_view text = as_text(bytes);
    // Some editors save a hand edit with a UTF-8 byte order mark.
    if (text.starts_with("\xEF\xBB\xBF")) text.remove_prefix(3);
    std::optional<json::value> parsed = parse_json(text);
    if (!parsed || !parsed->is_object()) return std::nullopt;
    const json::object& object = parsed->get_object();
    const json::value* schema = object.if_contains("schema");
    const json::value* revision = object.if_contains("revision");
    const json::value* values = object.if_contains("values");
    if (schema == nullptr || revision == nullptr || values == nullptr || !values->is_object()) return std::nullopt;
    const Result<u64> schema_number = u64_from_json(*schema);
    const Result<u64> revision_number = u64_from_json(*revision);
    if (!schema_number || *schema_number == 0 || *schema_number > std::numeric_limits<u32>::max() || !revision_number)
        return std::nullopt;
    return Envelope{static_cast<u32>(*schema_number), *revision_number, values->get_object()};
}

[[nodiscard]] std::vector<u8> encode_envelope(u32 schema, u64 revision, const json::object& values) {
    json::object envelope;
    envelope.emplace("schema", schema);
    envelope.emplace("revision", revision);
    envelope.emplace("values", values);
    const std::string text = to_pretty_json(envelope);
    const std::span<const u8> bytes = as_bytes(text);
    return {bytes.begin(), bytes.end()};
}

// 20261007T235829Z.
[[nodiscard]] std::string utc_stamp(std::chrono::system_clock::time_point time) {
    using namespace std::chrono;
    const auto seconds_since_epoch = floor<seconds>(time);
    const auto day = floor<days>(seconds_since_epoch);
    const year_month_day date{day};
    const hh_mm_ss clock_time{seconds_since_epoch - day};
    return std::format("{:04}{:02}{:02}T{:02}{:02}{:02}Z", static_cast<int>(date.year()),
                       static_cast<unsigned>(date.month()), static_cast<unsigned>(date.day()),
                       clock_time.hours().count(), clock_time.minutes().count(), clock_time.seconds().count());
}

[[nodiscard]] NativePath with_suffix(NativePath path, std::string_view suffix) {
    path += suffix;
    return path;
}

void add_member(std::vector<std::string>& members, std::string_view name) {
    if (std::ranges::find(members, name) == members.end()) members.emplace_back(name);
}

// What a worker found or did: a hand edit to merge, or the file's new revision after a write.
struct DiskOutcome {
    std::optional<std::vector<u8>> hand_edit;
    std::optional<ports::FileRevision> revision;
};

}  // namespace

struct DocumentFile::Impl {
    Impl(ports::IFileSystem& file_system, WorkerPool& worker_pool, Executor& strand_executor, const IClock& time,
         NativePath file, DocumentFormat document_format)
        : fs(file_system),
          workers(worker_pool),
          strand(strand_executor),
          clock(time),
          path(std::move(file)),
          format(document_format),
          waiters(strand_executor, std::string(document_format.name)) {}

    ports::IFileSystem& fs;
    WorkerPool& workers;
    Executor& strand;
    const IClock& clock;
    NativePath path;
    DocumentFormat format;

    json::object values;
    u64 revision = 0;
    // Why the mode is ReadOnly; replace() fails with it.
    std::optional<Diagnostic> read_only_reason;
    StorageMode mode = StorageMode::ReadWrite;
    // False once the store is memory-only for good; a failed write keeps it true and retries.
    bool disk = true;
    bool write_failed = false;
    // The primary did not parse, so the first write must not keep it as the .bak.
    bool primary_unreadable = false;
    // The file as the engine last wrote or loaded it; anything else is a hand edit.
    std::optional<ports::FileRevision> on_disk;
    // Top-level members changed since the last successful write, laid over a hand edit.
    std::vector<std::string> dirty;
    std::vector<std::string> in_flight;
    bool busy = false;
    FlushWaiters waiters;
    UniqueFunction<void(const json::object&)> on_reload;
    UniqueFunction<void(const StorageModeChanged&)> on_mode_changed;
    // Cancelled on destruction, so replies that arrive later never touch this object.
    CancelSource alive;

    void start_disk_job(bool write);
    void on_disk_job(bool write, Result<DiskOutcome> outcome);
    void merge_hand_edit(std::span<const u8> bytes);
    void set_write_failed(std::optional<Diagnostic> error);
};

void DocumentFile::Impl::start_disk_job(bool write) {
    busy = true;
    std::vector<u8> bytes;
    if (write) {
        in_flight = std::exchange(dirty, {});
        bytes = encode_envelope(format.schema, revision, values);
    }
    workers.submit<DiskOutcome>(
        [&fs = fs, path = path, bytes = std::move(bytes), expected = on_disk, write,
         keep_backup = !primary_unreadable](CancelToken) -> Result<DiskOutcome> {
            Result<ports::FileRevision> current = fs.revision(path);
            if (!current && current.error().kind != ErrorKind::NotFound) return std::unexpected(current.error());
            const bool edited = current ? expected != *current : expected.has_value();
            if (edited && current) {
                Result<std::vector<u8>> edit = fs.read_all(path);
                if (!edit) return std::unexpected(edit.error());
                return DiskOutcome{std::move(*edit), *current};
            }
            if (!write) return DiskOutcome{std::nullopt, expected};
            if (Result<void> replaced = fs.atomic_replace(path, bytes, keep_backup); !replaced)
                return std::unexpected(replaced.error());
            // The write landed even if the stat fails; the next job then rereads the file as a hand edit.
            Result<ports::FileRevision> after = fs.revision(path);
            return DiskOutcome{std::nullopt, after ? std::optional(*after) : std::nullopt};
        },
        CancelToken{}, strand, [this, alive_token = alive.token(), write](Result<DiskOutcome> outcome) {
            if (alive_token.cancelled()) return;
            on_disk_job(write, std::move(outcome));
        });
}

// `busy` stays set until the end, so a change made from a callback here waits for the next write.
void DocumentFile::Impl::on_disk_job(bool write, Result<DiskOutcome> outcome) {
    const std::vector<std::string> carried = std::exchange(in_flight, {});
    if (!outcome || outcome->hand_edit)
        for (const std::string& member : carried) add_member(dirty, member);
    if (!outcome && write) {
        Diagnostic error = make_diag(ErrorDomain::Storage, msg::kWriteFailed)
                               .arg("document", format.name)
                               .arg("path", path)
                               .cause(std::move(outcome.error()))
                               .build();
        set_write_failed(error);
        busy = false;
        waiters.finish(std::unexpected(std::move(error)));
        return;
    }
    // A refresh that cannot look at the file leaves the mode alone; nothing was lost.
    if (outcome) {
        on_disk = outcome->revision;
        if (outcome->hand_edit) {
            merge_hand_edit(*outcome->hand_edit);
        } else if (write) {
            primary_unreadable = false;
            set_write_failed(std::nullopt);
        }
    }
    busy = false;
    // Members changed during the write, or kept over a hand edit, go out in the next one.
    if (!dirty.empty()) {
        start_disk_job(true);
        return;
    }
    waiters.finish({});
}

void DocumentFile::Impl::merge_hand_edit(std::span<const u8> bytes) {
    std::optional<Envelope> edit = parse_envelope(bytes);
    // A broken or foreign-schema edit is overwritten by the next write.
    if (!edit || edit->schema != format.schema) return;
    json::object merged = std::move(edit->values);
    for (const std::string& member : dirty) {
        if (const json::value* ours = values.if_contains(member)) merged[member] = *ours;
        else merged.erase(member);
    }
    values = std::move(merged);
    revision = std::max(edit->revision, revision) + 1;
    if (on_reload) on_reload(values);
}

void DocumentFile::Impl::set_write_failed(std::optional<Diagnostic> error) {
    const bool failed = error.has_value();
    if (failed == write_failed) return;
    write_failed = failed;
    mode = failed ? StorageMode::InMemory : StorageMode::ReadWrite;
    if (on_mode_changed) on_mode_changed(StorageModeChanged{std::string(format.name), mode, std::move(error)});
}

DocumentFile::DocumentFile(ports::IFileSystem& fs, WorkerPool& workers, Executor& strand, const IClock& clock,
                           NativePath path, DocumentFormat format)
    : impl_(std::make_unique<Impl>(fs, workers, strand, clock, std::move(path), format)) {}

DocumentFile::~DocumentFile() { impl_->alive.cancel(CancelReason::Shutdown); }

LoadReport DocumentFile::load() {
    Impl& impl = *impl_;
    LoadReport report{.document = std::string(impl.format.name)};
    Result<std::vector<u8>> primary = impl.fs.read_all(impl.path);
    if (!primary && primary.error().kind != ErrorKind::NotFound)
        return load_memory_only(make_diag(ErrorDomain::Storage, msg::kMemoryOnly)
                                    .arg("document", impl.format.name)
                                    .arg("path", impl.path)
                                    .cause(std::move(primary.error()))
                                    .build());

    std::vector<u8> used;
    std::optional<Envelope> envelope;
    if (primary) {
        envelope = parse_envelope(*primary);
        if (envelope) used = *primary;
        report.source = LoadSource::Primary;
    }
    if (!envelope) {
        // Also covers a missing primary: a replace that failed after moving it aside leaves only the .bak.
        if (Result<std::vector<u8>> backup = impl.fs.read_all(with_suffix(impl.path, ".bak"))) {
            envelope = parse_envelope(*backup);
            if (envelope) used = std::move(*backup);
        }
        if (envelope) {
            report.source = LoadSource::Backup;
            report.reason = make_diag(ErrorDomain::Storage, msg::kRestoredFromBackup)
                                .severity(Severity::Warning)
                                .arg("document", impl.format.name)
                                .build();
        } else if (!primary) {
            return report;
        } else {
            const NativePath quarantine = with_suffix(impl.path, ".corrupt-" + utc_stamp(impl.clock.system_now()));
            if (Result<void> copied = impl.fs.atomic_replace(quarantine, *primary, false); !copied) {
                // Writing defaults now would destroy the only copy of the unreadable file.
                LoadReport memory_only = load_memory_only(make_diag(ErrorDomain::Storage, msg::kQuarantineFailed)
                                                              .arg("document", impl.format.name)
                                                              .arg("path", quarantine)
                                                              .cause(std::move(copied.error()))
                                                              .build());
                memory_only.source = LoadSource::Defaults;
                return memory_only;
            }
            report.source = LoadSource::Defaults;
            report.quarantined_to = quarantine;
            report.reason = make_diag(ErrorDomain::Storage, msg::kCorrupt)
                                .severity(Severity::Warning)
                                .arg("document", impl.format.name)
                                .arg("quarantine", quarantine)
                                .build();
        }
    }
    impl.primary_unreadable = report.source != LoadSource::Primary;
    if (Result<ports::FileRevision> stat = impl.fs.revision(impl.path)) impl.on_disk = *stat;
    if (!envelope) return report;

    report.schema_on_disk = envelope->schema;
    impl.revision = envelope->revision;
    impl.values = std::move(envelope->values);
    if (envelope->schema > impl.format.schema) {
        impl.mode = StorageMode::ReadOnly;
        report.reason = make_diag(ErrorDomain::Storage, msg::kReadOnly)
                            .severity(Severity::Warning)
                            .arg("document", impl.format.name)
                            .arg("schema", envelope->schema)
                            .build();
    } else if (envelope->schema < impl.format.schema) {
        const NativePath schema_backup =
            impl.path.parent_path() / std::format("{}.v{}.json", impl.format.name, envelope->schema);
        if (Result<void> saved = impl.fs.atomic_replace(schema_backup, used, false); !saved) {
            impl.mode = StorageMode::ReadOnly;
            report.reason = make_diag(ErrorDomain::Storage, msg::kSchemaBackupFailed)
                                .arg("document", impl.format.name)
                                .arg("path", schema_backup)
                                .cause(std::move(saved.error()))
                                .build();
        } else if (Result<json::object> upgraded = impl.format.upgrade(impl.values, envelope->schema); !upgraded) {
            report.schema_backup = schema_backup;
            impl.mode = StorageMode::ReadOnly;
            report.reason = make_diag(ErrorDomain::Storage, msg::kUpgradeFailed)
                                .arg("document", impl.format.name)
                                .arg("schema", envelope->schema)
                                .cause(std::move(upgraded.error()))
                                .build();
        } else {
            report.schema_backup = schema_backup;
            impl.values = std::move(*upgraded);
            for (const json::key_value_pair& member : impl.values)
                add_member(impl.dirty, std::string_view(member.key().data(), member.key().size()));
        }
    }
    if (impl.mode == StorageMode::ReadOnly) impl.read_only_reason = report.reason;
    report.mode = impl.mode;
    return report;
}

LoadReport DocumentFile::load_memory_only(Diagnostic reason) {
    Impl& impl = *impl_;
    impl.disk = false;
    impl.mode = StorageMode::InMemory;
    impl.values = {};
    impl.revision = 0;
    return LoadReport{.document = std::string(impl.format.name),
                      .mode = StorageMode::InMemory,
                      .source = LoadSource::Fresh,
                      .schema_on_disk = 0,
                      .quarantined_to = std::nullopt,
                      .schema_backup = std::nullopt,
                      .reason = std::move(reason),
                      .issues = {}};
}

const json::object& DocumentFile::values() const noexcept { return impl_->values; }

u64 DocumentFile::revision() const noexcept { return impl_->revision; }

StorageMode DocumentFile::mode() const noexcept { return impl_->mode; }

Result<u64> DocumentFile::replace(json::object values) {
    Impl& impl = *impl_;
    if (impl.mode == StorageMode::ReadOnly) {
        Diagnostic error = *impl.read_only_reason;
        error.severity = Severity::Error;
        error.kind = ErrorKind::Conflict;
        return std::unexpected(std::move(error));
    }
    for (const json::key_value_pair& member : values) {
        const json::value* old = impl.values.if_contains(member.key());
        if (old == nullptr || *old != member.value())
            add_member(impl.dirty, std::string_view(member.key().data(), member.key().size()));
    }
    for (const json::key_value_pair& member : impl.values)
        if (!values.contains(member.key()))
            add_member(impl.dirty, std::string_view(member.key().data(), member.key().size()));
    impl.values = std::move(values);
    ++impl.revision;
    if (impl.disk && !impl.busy) impl.start_disk_job(true);
    return impl.revision;
}

void DocumentFile::refresh() {
    Impl& impl = *impl_;
    if (!impl.disk || impl.busy || impl.mode == StorageMode::ReadOnly) return;
    impl.start_disk_job(false);
}

void DocumentFile::flush(CancelToken cancel, UniqueFunction<void(Result<void>)> done) {
    Impl& impl = *impl_;
    const bool pending = impl.busy || !impl.dirty.empty();
    if (!impl.disk || impl.mode == StorageMode::ReadOnly || !pending) {
        impl.strand.post([done = std::move(done)]() mutable { done({}); });
        return;
    }
    impl.waiters.add(cancel, std::move(done));
    if (!impl.busy) impl.start_disk_job(true);
}

void DocumentFile::set_on_reload(UniqueFunction<void(const json::object& values)> on_reload) {
    impl_->on_reload = std::move(on_reload);
}

void DocumentFile::set_on_mode_changed(UniqueFunction<void(const StorageModeChanged&)> on_mode_changed) {
    impl_->on_mode_changed = std::move(on_mode_changed);
}

}  // namespace reboot::storage
