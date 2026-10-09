#include "reboot/logging/log_exporter.hpp"

#include <archive.h>
#include <archive_entry.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <filesystem>
#include <format>
#include <fstream>
#include <memory>
#include <system_error>
#include <utility>
#include <vector>

#include "messages.hpp"
#include "reboot/foundation/executor.hpp"
#include "reboot/foundation/log.hpp"
#include "reboot/foundation/paths.hpp"
#include "reboot/foundation/text.hpp"
#include "reboot/logging/log_file_names.hpp"

namespace rb::logging {

namespace {

constexpr std::string_view kSummaryName = "runtime-summary.txt";
constexpr std::size_t kReadChunk = 64u << 10;
constexpr std::size_t kWriteChunk = 64u << 10;
// LogExport is a liveness kind, so long files report progress as they go.
constexpr u64 kProgressBytes = 4u << 20;

struct ArchiveFree {
    void operator()(archive* handle) const noexcept { archive_write_free(handle); }
};

struct EntryFree {
    void operator()(archive_entry* entry) const noexcept { archive_entry_free(entry); }
};

[[nodiscard]] Diagnostic write_failed(const NativePath& destination) {
    return make_diag(ErrorDomain::Logging, msg::kExportWriteFailed).arg("path", destination).build();
}

[[nodiscard]] Diagnostic write_failed(const NativePath& destination, const std::error_code& error) {
    return make_diag(ErrorDomain::Logging, msg::kExportWriteFailed)
        .arg("path", destination)
        .os(SystemError{SystemError::Origin::Host, error.value()})
        .build();
}

[[nodiscard]] Diagnostic archive_failed(const NativePath& destination, archive* handle) {
    const char* reason = archive_error_string(handle);
    return make_diag(ErrorDomain::Logging, msg::kExportWriteFailed)
        .arg("path", destination)
        .detail(reason != nullptr ? std::string(reason) : std::string("libarchive"))
        .build();
}

[[nodiscard]] bool is_valid_destination(const NativePath& destination, const NativePath& logs_dir) {
    return destination.is_absolute() && destination.has_filename() &&
           iequals_ascii(display_utf8(destination.extension()), ".zip") && !is_inside(destination, logs_dir);
}

// Through the two clocks' current offset: file_clock::to_sys and clock_cast are not on every floor toolchain.
[[nodiscard]] std::chrono::system_clock::time_point modified_time(const NativePath& path) {
    std::error_code error;
    const auto written_at = std::filesystem::last_write_time(path, error);
    const auto now = std::chrono::system_clock::now();
    if (error) return now;
    return std::chrono::time_point_cast<std::chrono::system_clock::duration>(
        now + (written_at - std::filesystem::file_time_type::clock::now()));
}

// Removes the partial archive unless the export completed.
class PartialFile {
public:
    explicit PartialFile(NativePath path) : path_(std::move(path)) {}
    ~PartialFile() {
        if (keep_) return;
        std::error_code ignored;
        std::filesystem::remove(path_, ignored);
    }
    PartialFile(const PartialFile&) = delete;
    PartialFile& operator=(const PartialFile&) = delete;

    [[nodiscard]] const NativePath& path() const noexcept { return path_; }
    void keep() noexcept { keep_ = true; }

private:
    NativePath path_;
    bool keep_ = false;
};

la_ssize_t write_output(archive*, void* client, const void* buffer, std::size_t length) {
    auto& out = *static_cast<std::ofstream*>(client);
    out.write(static_cast<const char*>(buffer), static_cast<std::streamsize>(length));
    return out ? static_cast<la_ssize_t>(length) : -1;
}

int close_output(archive*, void* client) {
    auto& out = *static_cast<std::ofstream*>(client);
    out.close();
    return out ? ARCHIVE_OK : ARCHIVE_FATAL;
}

class ExportJob {
public:
    ExportJob(NativePath logs_dir, LogExportRequest request, NativePath partial, const Redactor& redactor,
              Executor& strand, OperationBase& op)
        : logs_dir_(std::move(logs_dir)),
          request_(std::move(request)),
          partial_(std::move(partial)),
          redactor_(redactor),
          strand_(strand),
          op_(op) {}

    Result<LogExportResult> run(const CancelToken& token) {
        Result<std::vector<NativePath>> logs = list_logs();
        if (!logs) return std::unexpected(std::move(logs.error()));
        total_ = logs->size();

        PartialFile partial(partial_);
        std::ofstream out(partial.path(), std::ios::binary | std::ios::trunc);
        if (!out) return std::unexpected(write_failed(request_.destination));
        const std::unique_ptr<archive, ArchiveFree> zip(archive_write_new());
        if (!zip) return std::unexpected(write_failed(request_.destination));
        archive_ = zip.get();
        if (archive_write_set_format_zip(archive_) != ARCHIVE_OK ||
            archive_write_set_bytes_in_last_block(archive_, 1) != ARCHIVE_OK ||
            archive_write_open2(archive_, &out, nullptr, &write_output, &close_output, nullptr) != ARCHIVE_OK)
            return std::unexpected(archive_failed(request_.destination, archive_));

        if (Result<void> summary = add_summary(); !summary) return std::unexpected(std::move(summary.error()));
        LogExportResult result;
        for (const NativePath& log : *logs) {
            if (token.cancelled()) return std::unexpected(cancelled());
            Result<bool> added = add_log(log, token);
            if (!added) return std::unexpected(std::move(added.error()));
            if (*added) ++result.files;
            else ++result.skipped;
            ++done_;
            report_progress();
        }
        if (archive_write_close(archive_) != ARCHIVE_OK)
            return std::unexpected(archive_failed(request_.destination, archive_));
        if (token.cancelled()) return std::unexpected(cancelled());

        std::error_code error;
        std::filesystem::rename(partial.path(), request_.destination, error);
        if (error) return std::unexpected(write_failed(request_.destination, error));
        partial.keep();
        result.archive = request_.destination;
        result.bytes = std::filesystem::file_size(request_.destination, error);
        if (error) result.bytes = 0;
        return result;
    }

private:
    [[nodiscard]] Diagnostic cancelled() const {
        return make_diag(ErrorDomain::Logging, msg::kExportCancelled)
            .arg("path", request_.destination)
            .kind(ErrorKind::Cancelled)
            .build();
    }

    [[nodiscard]] Diagnostic read_failed(const NativePath& path) const {
        return make_diag(ErrorDomain::Logging, msg::kExportReadFailed).arg("path", path).build();
    }

    [[nodiscard]] Diagnostic read_failed(const NativePath& path, const std::error_code& error) const {
        return make_diag(ErrorDomain::Logging, msg::kExportReadFailed)
            .arg("path", path)
            .os(SystemError{SystemError::Origin::Host, error.value()})
            .build();
    }

    // Session, Wine and Proton logs by name; a missing directory exports only the summary.
    Result<std::vector<NativePath>> list_logs() const {
        std::vector<NativePath> logs;
        std::error_code error;
        std::filesystem::directory_iterator it(logs_dir_, error);
        if (error == std::errc::no_such_file_or_directory) return logs;
        if (error) return std::unexpected(read_failed(logs_dir_, error));
        for (; it != std::filesystem::directory_iterator(); it.increment(error)) {
            if (error) return std::unexpected(read_failed(logs_dir_, error));
            std::error_code status_error;
            if (it->is_symlink(status_error) || !it->is_regular_file(status_error)) continue;
            if (classify_log_file(display_utf8(it->path().filename()))) logs.push_back(it->path());
        }
        if (error) return std::unexpected(read_failed(logs_dir_, error));
        std::ranges::sort(logs);
        return logs;
    }

    Result<void> begin_entry(std::string_view name, std::chrono::system_clock::time_point modified) {
        const std::unique_ptr<archive_entry, EntryFree> entry(archive_entry_new());
        if (!entry) return std::unexpected(write_failed(request_.destination));
        archive_entry_set_pathname(entry.get(), std::string(name).c_str());
        archive_entry_set_filetype(entry.get(), AE_IFREG);
        archive_entry_set_perm(entry.get(), 0600);
        archive_entry_set_mtime(entry.get(), std::chrono::system_clock::to_time_t(modified), 0);
        // Redaction changes the size, so the zip records it after the data.
        archive_entry_unset_size(entry.get());
        if (archive_write_header(archive_, entry.get()) != ARCHIVE_OK)
            return std::unexpected(archive_failed(request_.destination, archive_));
        return {};
    }

    Result<void> write_data(std::string_view data) {
        while (!data.empty()) {
            const la_ssize_t written = archive_write_data(archive_, data.data(), data.size());
            if (written <= 0) return std::unexpected(archive_failed(request_.destination, archive_));
            data.remove_prefix(static_cast<std::size_t>(written));
        }
        return {};
    }

    // Every complete line in `text` goes through the Redactor; the unfinished tail stays.
    Result<void> redact_lines(std::string& text, bool final_chunk) {
        std::string out;
        std::size_t start = 0;
        for (std::size_t end = text.find('\n'); end != std::string::npos; end = text.find('\n', start)) {
            out += redactor_.apply(std::string_view(text).substr(start, end - start));
            out += '\n';
            start = end + 1;
        }
        if (final_chunk && start < text.size()) {
            out += redactor_.apply(std::string_view(text).substr(start));
            start = text.size();
        }
        text.erase(0, start);
        return write_data(out);
    }

    Result<void> add_summary() {
        if (Result<void> begun = begin_entry(kSummaryName, std::chrono::system_clock::now()); !begun) return begun;
        std::string summary = request_.runtime_summary;
        return redact_lines(summary, true);
    }

    // False when the file was pruned or removed after the listing.
    Result<bool> add_log(const NativePath& path, const CancelToken& token) {
        std::ifstream in(path, std::ios::binary);
        if (!in) {
            std::error_code error;
            if (!std::filesystem::exists(path, error) && !error) return false;
            return std::unexpected(read_failed(path));
        }
        if (Result<void> begun = begin_entry(display_utf8(path.filename()), modified_time(path)); !begun)
            return std::unexpected(std::move(begun.error()));

        std::string pending;
        std::array<char, kReadChunk> chunk{};
        u64 since_progress = 0;
        while (in) {
            if (token.cancelled()) return std::unexpected(cancelled());
            in.read(chunk.data(), static_cast<std::streamsize>(chunk.size()));
            const auto got = static_cast<std::size_t>(in.gcount());
            if (got == 0) break;
            pending.append(chunk.data(), got);
            if (pending.size() >= kWriteChunk)
                if (Result<void> written = redact_lines(pending, false); !written)
                    return std::unexpected(std::move(written.error()));
            since_progress += got;
            if (since_progress >= kProgressBytes) {
                since_progress = 0;
                report_progress();
            }
        }
        if (in.bad()) return std::unexpected(read_failed(path));
        if (Result<void> written = redact_lines(pending, true); !written) return std::unexpected(std::move(written.error()));
        return true;
    }

    // The op stays valid until its completion, which is posted after this.
    void report_progress() {
        strand_.post([&op = op_, done = done_, total = total_] {
            op.progress(Progress{.phase = "exporting", .done = done, .total = total});
        });
    }

    NativePath logs_dir_;
    LogExportRequest request_;
    NativePath partial_;
    const Redactor& redactor_;
    Executor& strand_;
    OperationBase& op_;
    archive* archive_ = nullptr;
    u64 done_ = 0;
    u64 total_ = 0;
};

}  // namespace

struct LogExporter::Impl {
    Impl(NativePath logs_dir_value, const Redactor& redactor_ref, OpRegistry& ops_ref, WorkerPool& workers_ref,
         Executor& strand_ref)
        : logs_dir(std::move(logs_dir_value)), redactor(redactor_ref), ops(ops_ref), workers(workers_ref), strand(strand_ref) {}

    const NativePath logs_dir;
    const Redactor& redactor;
    OpRegistry& ops;
    WorkerPool& workers;
    Executor& strand;
};

LogExporter::LogExporter(NativePath logs_dir, const Redactor& redactor, OpRegistry& ops, WorkerPool& workers,
                         Executor& strand)
    : impl_(std::make_unique<Impl>(std::move(logs_dir), redactor, ops, workers, strand)) {}

LogExporter::~LogExporter() = default;

Result<OpHandle> LogExporter::start_export(LogExportRequest request, DisconnectPolicy policy) {
    if (!is_valid_destination(request.destination, impl_->logs_dir))
        return make_diag(ErrorDomain::Logging, msg::kExportDestinationInvalid)
            .arg("path", request.destination)
            .kind(ErrorKind::InvalidInput)
            .fail();

    auto created = impl_->ops.create<LogExportResult>(OpKind::LogExport, policy, std::nullopt);
    const OpHandle handle = created.first;
    Operation<LogExportResult>& op = created.second;
    NativePath partial = request.destination;
    partial += std::format(".{}.partial", handle.id().value);
    auto job = std::make_unique<ExportJob>(impl_->logs_dir, std::move(request), std::move(partial), impl_->redactor,
                                           impl_->strand, op);
    impl_->workers.submit<LogExportResult>(
        [job = std::move(job)](CancelToken token) { return job->run(token); }, op.token(), impl_->strand,
        [&op](Result<LogExportResult> result) {
            if (result) op.complete(Completed<LogExportResult>{std::move(*result)});
            else op.complete(Failed{std::move(result.error())});
        });
    return handle;
}

}  // namespace rb::logging
