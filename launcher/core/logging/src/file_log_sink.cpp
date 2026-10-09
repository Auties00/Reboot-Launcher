#include "reboot/logging/file_log_sink.hpp"

#include <mutex>
#include <utility>
#include <vector>

#include "messages.hpp"
#include "reboot/foundation/clock.hpp"
#include "reboot/logging/log_files_in_use.hpp"
#include "reboot/logging/log_format.hpp"
#include "reboot/logging/wine_log_files.hpp"
#include "reboot/ports/log_file_system.hpp"

namespace reboot::logging {

namespace {

[[nodiscard]] std::span<const u8> bytes_of(std::string_view text) noexcept {
    return {reinterpret_cast<const u8*>(text.data()), text.size()};
}

[[nodiscard]] Diagnostic file_failure(MessageId message, const NativePath& path, Diagnostic cause) {
    return make_diag(ErrorDomain::Logging, message).arg("path", path).cause(std::move(cause)).build();
}

}  // namespace

struct FileLogSink::Impl {
    Impl(FileLogOptions options_value, const IClock& clock_ref, ports::ILogFileSystem& files_ref,
         LogFilesInUse& in_use_ref, std::unique_ptr<WineLogFiles> wine_logs)
        : options(std::move(options_value)),
          clock(clock_ref),
          files(files_ref),
          in_use(in_use_ref),
          wine(std::move(wine_logs)),
          current_file(part_path(0)) {}

    [[nodiscard]] NativePath part_path(u32 number) const {
        return options.dir / session_log_file_name(options.group, options.role, number);
    }

    [[nodiscard]] u64 part_size() const noexcept { return file.is_open() ? file.size() : 0; }

    void record_failure(Diagnostic failure, std::vector<Diagnostic>& to_report) {
        {
            const std::lock_guard lock(status_mutex);
            ++failed_writes;
            last_failure = failure;
        }
        if (failing) return;
        failing = true;
        to_report.push_back(std::move(failure));
    }

    // A part whose open failed is retried on each later write.
    bool ensure_open(std::vector<Diagnostic>& to_report) {
        if (file.is_open()) return true;
        Result<ports::LogFile> opened = files.open_append(current_file);
        if (!opened) {
            record_failure(file_failure(msg::kOpenFailed, current_file, std::move(opened.error())), to_report);
            return false;
        }
        file = std::move(*opened);
        in_use.add(current_file);
        failing = false;
        return true;
    }

    void write_part(std::string& pending, std::vector<Diagnostic>& to_report) {
        if (pending.empty()) return;
        if (ensure_open(to_report)) {
            if (Result<void> written = file.append(bytes_of(pending)); written) {
                failing = false;
            } else {
                record_failure(file_failure(msg::kWriteFailed, current_file, std::move(written.error())), to_report);
            }
        }
        pending.clear();
    }

    void roll(std::vector<Diagnostic>& to_report) {
        if (file.is_open()) {
            file.close();
            in_use.remove(current_file);
        }
        {
            const std::lock_guard lock(status_mutex);
            ++part;
            current_file = part_path(part);
        }
        failing = false;
        ensure_open(to_report);
        prune();
    }

    // Errors are ignored: a file another process holds is retried at the next roll.
    void prune() {
        Result<std::vector<ports::LogDirEntry>> listed = files.list(options.dir);
        if (!listed) return;
        // Both sides normalised: the port may spell the directory differently from options.dir.
        std::vector<LogFileInfo> logs;
        for (ports::LogDirEntry& entry : *listed) {
            const std::optional<ParsedLogFileName> parsed = classify_log_file(display_utf8(entry.path.filename()));
            if (!parsed) continue;
            logs.push_back(LogFileInfo{(options.dir / entry.path.filename()).lexically_normal(), parsed->kind,
                                       parsed->group, entry.size, entry.modified});
        }
        std::vector<NativePath> busy = in_use.snapshot();
        for (NativePath& path : busy) path = path.lexically_normal();
        for (const NativePath& expired : select_expired(logs, options.retention, clock.system_now(), busy))
            (void)files.remove(expired);
    }

    // WineLogFiles counts each failed attempt once, and never retries a failed session by itself.
    void collect_wine_failure(std::vector<Diagnostic>& to_report) {
        WineLogStatus wine_status = wine->status();
        if (wine_status.failed_writes == wine_failures_seen) return;
        wine_failures_seen = wine_status.failed_writes;
        if (wine_status.last_failure) to_report.push_back(std::move(*wine_status.last_failure));
    }

    void report(const std::vector<Diagnostic>& failures) {
        if (failures.empty()) return;
        const std::lock_guard lock(callback_mutex);
        if (!on_failure) return;
        for (const Diagnostic& failure : failures) on_failure(failure);
    }

    const FileLogOptions options;
    const IClock& clock;
    ports::ILogFileSystem& files;
    LogFilesInUse& in_use;
    const std::unique_ptr<WineLogFiles> wine;

    // Serialises write and flush, and is held across file calls.
    std::mutex mutex;
    ports::LogFile file;
    // Written under both mutexes; status() takes only this one, so it never waits on file I/O.
    mutable std::mutex status_mutex;
    NativePath current_file;
    u32 part = 0;
    u64 failed_writes = 0;
    std::optional<Diagnostic> last_failure;
    // A failure was reported since the part opened or last took a write.
    bool failing = false;
    u64 wine_failures_seen = 0;

    // Held while the callback runs, so set_on_failure can wait it out.
    std::mutex callback_mutex;
    UniqueFunction<void(const Diagnostic&)> on_failure;
};

Result<std::unique_ptr<FileLogSink>> FileLogSink::open(FileLogOptions options, const IClock& clock,
                                                       ports::ILogFileSystem& files, LogFilesInUse& in_use,
                                                       std::unique_ptr<WineLogFiles> wine) {
    if (!is_valid_log_role(options.role)) return std::unexpected(internal_bug("logging.file_log_sink.role"));
    if (Result<void> created = files.create_directories(options.dir); !created)
        return std::unexpected(file_failure(msg::kDirectoryFailed, options.dir, std::move(created.error())));

    auto impl = std::make_unique<Impl>(std::move(options), clock, files, in_use, std::move(wine));
    Result<ports::LogFile> opened = files.open_append(impl->current_file);
    if (!opened) return std::unexpected(file_failure(msg::kOpenFailed, impl->current_file, std::move(opened.error())));
    impl->file = std::move(*opened);
    in_use.add(impl->current_file);
    impl->prune();
    return std::unique_ptr<FileLogSink>(new FileLogSink(std::move(impl)));
}

FileLogSink::FileLogSink(std::unique_ptr<Impl> impl) : impl_(std::move(impl)) {}

FileLogSink::~FileLogSink() {
    if (impl_->file.is_open()) {
        impl_->file.close();
        impl_->in_use.remove(impl_->current_file);
    }
}

void FileLogSink::write(std::span<const LogRecord> records) {
    std::vector<Diagnostic> to_report;
    {
        const std::lock_guard lock(impl_->mutex);
        std::string pending;
        for (const LogRecord& record : records) {
            std::string line = format_log_line(record);
            if (impl_->wine && routes_to_wine_log(record)) {
                if (impl_->wine->append(*record.session, line)) continue;
                impl_->collect_wine_failure(to_report);
            }
            const u64 filled = impl_->part_size() + pending.size();
            if (filled > 0 && filled + line.size() > impl_->options.roll_bytes) {
                impl_->write_part(pending, to_report);
                impl_->roll(to_report);
            }
            pending += line;
        }
        impl_->write_part(pending, to_report);
    }
    impl_->report(to_report);
}

void FileLogSink::flush() {
    std::vector<Diagnostic> to_report;
    {
        const std::lock_guard lock(impl_->mutex);
        if (impl_->file.is_open()) {
            if (Result<void> flushed = impl_->file.flush(); !flushed)
                impl_->record_failure(file_failure(msg::kWriteFailed, impl_->current_file, std::move(flushed.error())),
                                      to_report);
        }
        if (impl_->wine) {
            impl_->wine->flush();
            impl_->collect_wine_failure(to_report);
        }
    }
    impl_->report(to_report);
}

FileLogStatus FileLogSink::status() const {
    const std::lock_guard lock(impl_->status_mutex);
    return FileLogStatus{impl_->current_file, impl_->part, impl_->failed_writes, impl_->last_failure};
}

WineLogFiles* FileLogSink::wine_logs() noexcept { return impl_->wine.get(); }

void FileLogSink::set_on_failure(UniqueFunction<void(const Diagnostic&)> on_failure) {
    UniqueFunction<void(const Diagnostic&)> previous;
    {
        const std::lock_guard lock(impl_->callback_mutex);
        previous = std::exchange(impl_->on_failure, std::move(on_failure));
    }
}

}  // namespace reboot::logging
