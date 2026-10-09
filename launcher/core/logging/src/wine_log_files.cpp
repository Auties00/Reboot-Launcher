#include "reboot/logging/wine_log_files.hpp"

#include <format>
#include <map>
#include <mutex>
#include <span>
#include <utility>

#include "messages.hpp"
#include "reboot/logging/log_files_in_use.hpp"
#include "reboot/ports/log_file_system.hpp"

namespace rb::logging {

namespace {

[[nodiscard]] std::span<const u8> bytes_of(std::string_view text) noexcept {
    return {reinterpret_cast<const u8*>(text.data()), text.size()};
}

[[nodiscard]] Diagnostic file_failure(MessageId message, const NativePath& path, Diagnostic cause) {
    return make_diag(ErrorDomain::Logging, message).arg("path", path).cause(std::move(cause)).build();
}

}  // namespace

bool routes_to_wine_log(const LogRecord& record) noexcept {
    return record.category == LogCategory::Wine && record.session.has_value();
}

struct WineLogFiles::Impl {
    struct SessionFile {
        NativePath path;
        ports::LogFile file;
        bool truncated = false;
        // Set once a write failed; the session is not retried until close_session.
        std::optional<Diagnostic> failure;
    };

    Impl(WineLogOptions options_value, ports::ILogFileSystem& files_ref, LogFilesInUse& in_use_ref)
        : options(std::move(options_value)), files(files_ref), in_use(in_use_ref) {}

    void close(SessionFile& entry) {
        if (!entry.file.is_open()) return;
        entry.file.close();
        in_use.remove(entry.path);
    }

    Diagnostic fail(SessionFile& entry, Diagnostic failure) {
        ++status.failed_writes;
        status.last_failure = failure;
        entry.failure = failure;
        close(entry);
        return failure;
    }

    Result<void> write(SessionFile& entry, std::string_view text) {
        if (Result<void> written = entry.file.append(bytes_of(text)); !written)
            return std::unexpected(fail(entry, file_failure(msg::kWriteFailed, entry.path, std::move(written.error()))));
        return {};
    }

    const WineLogOptions options;
    ports::ILogFileSystem& files;
    LogFilesInUse& in_use;
    // Held across file calls too: close_session and status come from other threads.
    mutable std::mutex mutex;
    std::map<SessionId, SessionFile> sessions;
    WineLogStatus status;
};

WineLogFiles::WineLogFiles(WineLogOptions options, ports::ILogFileSystem& files, LogFilesInUse& in_use)
    : impl_(std::make_unique<Impl>(std::move(options), files, in_use)) {}

WineLogFiles::~WineLogFiles() {
    const std::lock_guard lock(impl_->mutex);
    for (auto& entry : impl_->sessions) impl_->close(entry.second);
}

Result<void> WineLogFiles::append(const SessionId& session, std::string_view line) {
    const std::lock_guard lock(impl_->mutex);
    Impl::SessionFile& entry = impl_->sessions[session];
    if (entry.truncated) {
        ++impl_->status.dropped_lines;
        return {};
    }
    if (entry.failure) return std::unexpected(*entry.failure);

    if (!entry.file.is_open()) {
        entry.path = impl_->options.dir / wine_log_file_name(impl_->options.group, session);
        Result<ports::LogFile> opened = impl_->files.open_append(entry.path);
        if (!opened)
            return std::unexpected(impl_->fail(entry, file_failure(msg::kOpenFailed, entry.path, std::move(opened.error()))));
        entry.file = std::move(*opened);
        // A reopened file past the cap already ends in the truncation line.
        entry.truncated = entry.file.size() >= impl_->options.cap_bytes;
        impl_->in_use.add(entry.path);
    }

    if (entry.truncated) {
        ++impl_->status.dropped_lines;
        return {};
    }
    if (entry.file.size() + line.size() > impl_->options.cap_bytes) {
        const std::string notice =
            std::format("[wine log reached its {} byte cap; later lines are dropped]\n", impl_->options.cap_bytes);
        if (Result<void> written = impl_->write(entry, notice); !written) return written;
        entry.truncated = true;
        ++impl_->status.dropped_lines;
        return {};
    }
    return impl_->write(entry, line);
}

void WineLogFiles::flush() {
    const std::lock_guard lock(impl_->mutex);
    for (auto& [session, entry] : impl_->sessions) {
        if (!entry.file.is_open()) continue;
        if (Result<void> flushed = entry.file.flush(); !flushed)
            impl_->fail(entry, file_failure(msg::kWriteFailed, entry.path, std::move(flushed.error())));
    }
}

void WineLogFiles::close_session(const SessionId& session) {
    const std::lock_guard lock(impl_->mutex);
    const auto it = impl_->sessions.find(session);
    if (it == impl_->sessions.end()) return;
    impl_->close(it->second);
    // The notice may leave the file under the cap, so a truncated session stays truncated.
    if (!it->second.truncated) impl_->sessions.erase(it);
}

WineLogStatus WineLogFiles::status() const {
    const std::lock_guard lock(impl_->mutex);
    return impl_->status;
}

}  // namespace rb::logging
