#include "reboot/net/resumable_downloader.hpp"

#include <algorithm>
#include <deque>
#include <filesystem>
#include <fstream>
#include <map>
#include <mutex>
#include <system_error>
#include <utility>
#include <vector>

#include "download_support.hpp"
#include "reboot/foundation/clock.hpp"
#include "reboot/foundation/executor.hpp"
#include "reboot/net/download_error.hpp"
#include "reboot/net/http_client.hpp"
#include "reboot/net/http_error.hpp"
#include "reboot/net/http_request.hpp"
#include "reboot/net/http_response.hpp"
#include "reboot/ports/file_system.hpp"
#include "url.hpp"

namespace rb::net {

namespace {

constexpr std::chrono::milliseconds kProgressInterval{100};
constexpr std::chrono::seconds kRateWindow{1};

[[nodiscard]] NativePath sidecar_path(const NativePath& file) {
    NativePath out = file;
    out += kResumeSidecarSuffix;
    return out;
}

[[nodiscard]] bool retryable_status(u32 status) noexcept {
    return status == 408 || status == 429 || (status >= 500 && status <= 599);
}

enum class Issue : u8 { None, RangeNotHonored, SourceChanged, HttpStatus, SizeMismatch };

// What one attempt shares between the strand, the transport's thread and the writer.
struct Pipe {
    Pipe(ports::IFileSystem& fs_in, ports::IDiskInfo& disk_in, WorkerPool& workers_in, NativePath file_in, std::string url_in)
        : fs(fs_in), disk(disk_in), workers(workers_in), file(std::move(file_in)), sidecar(sidecar_path(file)), url(std::move(url_in)) {}

    ports::IFileSystem& fs;
    ports::IDiskInfo& disk;
    WorkerPool& workers;
    const NativePath file;
    const NativePath sidecar;
    const std::string url;
    // Runs on the strand when the writer goes idle; set once by the strand before any attempt.
    UniqueFunction<void(u64 written, bool idle)> report;
    Executor* strand = nullptr;

    std::mutex mutex;
    // Set by the strand before each attempt.
    u32 attempt = 0;
    u64 offset = 0;
    std::string expected_validator;
    std::optional<u64> known_total;
    std::optional<u64> expected_size;
    bool space_checked = false;
    // From the response.
    bool accepted = false;
    Issue issue = Issue::None;
    u32 status = 0;
    std::optional<u64> total;
    std::string validator;
    // Writer work.
    bool truncate_pending = false;
    bool sidecar_pending = false;
    std::optional<u64> space_needed;
    std::deque<std::vector<u8>> queue;
    // Received but not yet written, counting the batch the writer holds.
    std::size_t queued_bytes = 0;
    bool writer_busy = false;
    bool backlog_exceeded = false;
    std::optional<DownloadError> write_error;
    u64 written = 0;
    // Only the writer job touches the stream, and only one writer job runs at a time.
    std::ofstream out;
};

using PipePtr = std::shared_ptr<Pipe>;

[[nodiscard]] DownloadError io_error(const Pipe& pipe, u64 offset, std::optional<Diagnostic> cause = std::nullopt) {
    DownloadError error;
    error.code = DownloadErrorCode::Io;
    error.file = pipe.file;
    error.offset = offset;
    error.cause = std::move(cause);
    return error;
}

void kick_writer(const PipePtr& pipe);

// One writer job: the attempt's pending truncate, sidecar and space check, then the queue until empty.
Result<bool> drain(const PipePtr& pipe) {
    while (true) {
        std::deque<std::vector<u8>> batch;
        bool truncate = false;
        std::optional<ResumeSidecar> sidecar;
        std::optional<u64> space_needed;
        u64 at = 0;
        {
            const std::scoped_lock lock(pipe->mutex);
            if (pipe->queue.empty() && !pipe->truncate_pending && !pipe->sidecar_pending && !pipe->space_needed) {
                pipe->writer_busy = false;
                return true;
            }
            batch.swap(pipe->queue);
            truncate = std::exchange(pipe->truncate_pending, false);
            if (std::exchange(pipe->sidecar_pending, false))
                sidecar = ResumeSidecar{pipe->url, pipe->validator, pipe->total};
            space_needed = std::exchange(pipe->space_needed, std::nullopt);
            at = pipe->written;
            if (pipe->write_error) batch.clear();
        }
        std::size_t batch_bytes = 0;
        for (const std::vector<u8>& chunk : batch) batch_bytes += chunk.size();

        std::optional<DownloadError> failure;
        u64 wrote = 0;
        if (truncate) {
            pipe->out.close();
            pipe->out.clear();
            pipe->out.open(pipe->file, std::ios::binary | std::ios::out | std::ios::trunc);
            if (!pipe->out) failure = io_error(*pipe, 0);
            at = 0;
            // A sidecar left from older content must not describe the new bytes.
            if (!failure && !sidecar) (void)pipe->fs.remove_tree(pipe->sidecar);
        }
        if (!failure && sidecar) {
            if (Result<void> written = pipe->fs.atomic_replace(pipe->sidecar, encode_sidecar(*sidecar), false); !written)
                failure = io_error(*pipe, at, std::move(written.error()));
        }
        if (!failure && space_needed) {
            const Result<ports::VolumeInfo> volume = pipe->disk.volume_of(pipe->file.parent_path());
            if (volume && volume->free_bytes < *space_needed) {
                DownloadError error;
                error.code = DownloadErrorCode::InsufficientSpace;
                error.file = pipe->file;
                error.offset = at;
                error.needed_bytes = *space_needed;
                error.free_bytes = volume->free_bytes;
                failure = std::move(error);
            }
        }
        if (!failure && !batch.empty()) {
            if (!pipe->out.is_open()) {
                pipe->out.clear();
                pipe->out.open(pipe->file, std::ios::binary | std::ios::in | std::ios::out);
                // Only a fresh file may be created; a resumed one that vanished would gain a hole.
                if (!pipe->out && at == 0) {
                    pipe->out.clear();
                    pipe->out.open(pipe->file, std::ios::binary | std::ios::out);
                }
                if (pipe->out) pipe->out.seekp(static_cast<std::streamoff>(at));
            }
            for (const std::vector<u8>& chunk : batch) {
                if (!pipe->out) break;
                pipe->out.write(reinterpret_cast<const char*>(chunk.data()), static_cast<std::streamsize>(chunk.size()));
                if (pipe->out) wrote += chunk.size();
            }
            pipe->out.flush();
            if (!pipe->out) failure = io_error(*pipe, at + wrote);
        }

        u64 written = 0;
        {
            const std::scoped_lock lock(pipe->mutex);
            if (truncate) pipe->written = 0;
            pipe->written += wrote;
            written = pipe->written;
            pipe->queued_bytes -= std::min(pipe->queued_bytes, batch_bytes);
            if (failure && !pipe->write_error) {
                pipe->write_error = std::move(failure);
                pipe->queue.clear();
                pipe->queued_bytes = 0;
            }
        }
        if (wrote > 0)
            pipe->strand->post([pipe, written] {
                if (pipe->report) pipe->report(written, false);
            });
    }
}

void kick_writer(const PipePtr& pipe) {
    {
        const std::scoped_lock lock(pipe->mutex);
        if (pipe->writer_busy) return;
        pipe->writer_busy = true;
    }
    pipe->workers.submit<bool>([pipe](CancelToken) { return drain(pipe); }, CancelToken{}, *pipe->strand,
                               [pipe](Result<bool>) {
                                   u64 written = 0;
                                   {
                                       const std::scoped_lock lock(pipe->mutex);
                                       written = pipe->written;
                                   }
                                   if (pipe->report) pipe->report(written, true);
                               });
}

// Runs on the transport's thread.
void judge_headers(const PipePtr& pipe, u32 attempt, u32 status, const std::vector<ports::HttpHeader>& headers) {
    bool kick = false;
    {
        const std::scoped_lock lock(pipe->mutex);
        if (attempt != pipe->attempt) return;
        pipe->status = status;
        const std::string validator = response_validator(headers);
        const std::string* range_header = find_header(headers, "Content-Range");
        const std::optional<ContentRange> range = range_header ? parse_content_range(*range_header) : std::nullopt;
        if (pipe->offset > 0) {
            if (status == 206) {
                if (!range || range->first != pipe->offset) {
                    pipe->issue = Issue::RangeNotHonored;
                } else if (pipe->known_total && range->total && *range->total != *pipe->known_total) {
                    pipe->issue = Issue::SourceChanged;
                } else {
                    pipe->accepted = true;
                    pipe->total = range->total ? range->total : pipe->known_total;
                    pipe->validator = pipe->expected_validator;
                }
            } else if (status == 200) {
                pipe->issue = !validator.empty() && validator != pipe->expected_validator ? Issue::SourceChanged
                                                                                          : Issue::RangeNotHonored;
            } else if (status == 416) {
                pipe->issue = Issue::RangeNotHonored;
            } else {
                pipe->issue = Issue::HttpStatus;
            }
        } else if (status == 200 || (status == 206 && range && range->first == 0)) {
            pipe->accepted = true;
            if (status == 206) {
                pipe->total = range->total;
            } else if (const std::string* length = find_header(headers, "Content-Length")) {
                pipe->total = parse_decimal(*length);
            }
            pipe->validator = validator;
            pipe->truncate_pending = true;
            pipe->sidecar_pending = !validator.empty();
        } else {
            pipe->issue = Issue::HttpStatus;
        }
        if (pipe->accepted && pipe->expected_size && pipe->total && *pipe->total != *pipe->expected_size) {
            pipe->accepted = false;
            pipe->truncate_pending = false;
            pipe->sidecar_pending = false;
            pipe->issue = Issue::SizeMismatch;
        }
        if (pipe->accepted && !pipe->space_checked && pipe->total) {
            pipe->space_checked = true;
            pipe->space_needed = *pipe->total > pipe->offset ? *pipe->total - pipe->offset : 0;
        }
        kick = pipe->accepted;
    }
    if (kick) kick_writer(pipe);
}

// Runs on the transport's thread; false aborts the stream.
bool queue_chunk(const PipePtr& pipe, u32 attempt, std::span<const u8> chunk) {
    {
        const std::scoped_lock lock(pipe->mutex);
        if (attempt != pipe->attempt || !pipe->accepted || pipe->write_error || pipe->backlog_exceeded) return false;
        if (pipe->queued_bytes + chunk.size() > kDownloadWriteBacklog) {
            pipe->backlog_exceeded = true;
            return false;
        }
        pipe->queue.emplace_back(chunk.begin(), chunk.end());
        pipe->queued_bytes += chunk.size();
    }
    kick_writer(pipe);
    return true;
}

}  // namespace

struct ResumableDownloader::Impl {
    struct Download {
        u64 id = 0;
        DownloadRequest request;
        std::string host;
        PipePtr pipe;
        UniqueFunction<void(const DownloadProgress&)> on_progress;
        UniqueFunction<void(Result<DownloadResult>)> done;
        CancelRegistration user_cancel;
        bool cancelled = false;
        bool closing = false;
        CancelSource stream_cancel;
        bool stream_active = false;
        std::optional<Result<ports::HttpStatus>> stream_result;
        TimerHandle retry_timer;
        u32 attempts_used = 0;
        u32 range_failures = 0;
        u32 consecutive_failures = 0;
        std::optional<Diagnostic> last_error;
        std::string validator;
        std::optional<u64> total;
        bool resumed = false;
        u64 attempt_start = 0;
        // Progress.
        u64 done_bytes = 0;
        // The size the current attempt's response stated, before the attempt settles.
        std::optional<u64> response_total;
        TimerHandle progress_timer;
        std::optional<SteadyTime> last_emit;
        std::optional<std::pair<u64, std::optional<u64>>> last_emitted;
        std::deque<std::pair<SteadyTime, u64>> samples;
    };
    using DownloadPtr = std::shared_ptr<Download>;

    struct Prepared {
        u64 offset = 0;
        std::string validator;
        std::optional<u64> total;
        bool complete = false;
    };

    struct Core : std::enable_shared_from_this<Core> {
        Core(HttpClient& http_in, ports::IDiskInfo& disk_in, ports::IFileSystem& fs_in, WorkerPool& workers_in,
             Executor& strand_in, TimerService& timers_in, IClock& clock_in)
            : http(http_in), disk(disk_in), fs(fs_in), workers(workers_in), strand(strand_in), timers(timers_in), clock(clock_in) {}

        DownloadPtr find(u64 id) {
            const auto it = downloads.find(id);
            return it == downloads.end() ? nullptr : it->second;
        }

        void prepare(const DownloadPtr& download) {
            workers.submit<Prepared>(
                [pipe = download->pipe, expected = download->request.expected_size](CancelToken) -> Result<Prepared> {
                    Prepared out;
                    std::error_code error;
                    const bool exists = std::filesystem::is_regular_file(pipe->file, error);
                    const u64 size = exists ? std::filesystem::file_size(pipe->file, error) : 0;
                    Result<std::vector<u8>> bytes = pipe->fs.read_all(pipe->sidecar);
                    const std::optional<ResumeSidecar> sidecar = bytes ? decode_sidecar(*bytes) : std::nullopt;
                    if (sidecar && sidecar->url == pipe->url && exists && !error && size > 0 &&
                        (!sidecar->total || size <= *sidecar->total) && (!expected || !sidecar->total || *sidecar->total == *expected)) {
                        out.offset = size;
                        out.validator = sidecar->validator;
                        out.total = sidecar->total;
                        out.complete = sidecar->total && size == *sidecar->total;
                    }
                    if (expected && !out.complete) {
                        const u64 needed = *expected > out.offset ? *expected - out.offset : 0;
                        const Result<ports::VolumeInfo> volume = pipe->disk.volume_of(pipe->file.parent_path());
                        if (volume && volume->free_bytes < needed) {
                            DownloadError failure;
                            failure.code = DownloadErrorCode::InsufficientSpace;
                            failure.file = pipe->file;
                            failure.offset = out.offset;
                            failure.needed_bytes = needed;
                            failure.free_bytes = volume->free_bytes;
                            return std::unexpected(to_diagnostic(failure));
                        }
                    }
                    return out;
                },
                CancelToken{}, strand,
                [weak = weak_from_this(), id = download->id](Result<Prepared> prepared) {
                    if (const std::shared_ptr<Core> self = weak.lock()) self->prepared(id, std::move(prepared));
                });
        }

        void prepared(u64 id, Result<Prepared> prepared) {
            const DownloadPtr download = find(id);
            if (!download) return;
            if (download->cancelled) return finish(download, std::unexpected(cancelled_diag(*download)));
            if (!prepared) return finish(download, std::unexpected(std::move(prepared.error())));
            download->validator = prepared->validator;
            download->total = prepared->total;
            {
                const std::scoped_lock lock(download->pipe->mutex);
                download->pipe->written = prepared->offset;
                download->pipe->expected_size = download->request.expected_size;
                download->pipe->space_checked = download->request.expected_size.has_value();
            }
            download->done_bytes = prepared->offset;
            if (prepared->complete) {
                download->resumed = true;
                return succeed(download);
            }
            begin_attempt(download, true);
        }

        void begin_attempt(const DownloadPtr& download, bool spend) {
            if (download->cancelled) return finish(download, std::unexpected(cancelled_diag(*download)));
            if (spend) {
                if (download->attempts_used >= std::max<u32>(download->request.retry.max_attempts, 1)) return exhausted(download);
                ++download->attempts_used;
            }
            Pipe& pipe = *download->pipe;
            u64 offset = 0;
            u32 attempt = 0;
            {
                const std::scoped_lock lock(pipe.mutex);
                // Without a validator a resume could splice two versions, so it starts over.
                offset = download->validator.empty() ? 0 : pipe.written;
                if (download->total && offset > *download->total) offset = 0;
                attempt = ++pipe.attempt;
                pipe.offset = offset;
                pipe.written = offset;
                pipe.expected_validator = download->validator;
                pipe.known_total = download->total;
                pipe.accepted = false;
                pipe.issue = Issue::None;
                pipe.status = 0;
                pipe.total.reset();
                pipe.validator.clear();
                pipe.backlog_exceeded = false;
            }
            download->attempt_start = offset;
            download->response_total.reset();
            if (offset > 0 && download->total && offset == *download->total) {
                download->resumed = true;
                return succeed(download);
            }

            HttpRequest request;
            request.method = HttpMethod::Get;
            request.url = download->request.url;
            request.kind = HttpKind::Download;
            HttpLimits limits = limits_for(HttpKind::Download);
            limits.total = download->request.total_timeout;
            request.limits = limits;
            request.retry = kNoRetry;
            if (offset > 0) {
                request.headers.push_back({"Range", "bytes=" + std::to_string(offset) + "-"});
                request.headers.push_back({"If-Range", download->validator});
            }

            ports::HttpCallbacks callbacks;
            const PipePtr shared = download->pipe;
            callbacks.on_headers = [shared, attempt](ports::HttpStatus status, const std::vector<ports::HttpHeader>& headers) {
                judge_headers(shared, attempt, status.code, headers);
            };
            callbacks.on_body_chunk = [shared, attempt](std::span<const u8> chunk) { return queue_chunk(shared, attempt, chunk); };
            callbacks.on_done = [weak = weak_from_this(), id = download->id, attempt, &strand_ref = strand](
                                    Result<ports::HttpStatus> result) mutable {
                strand_ref.post([weak, id, attempt, result = std::move(result)]() mutable {
                    if (const std::shared_ptr<Core> self = weak.lock()) self->stream_ended(id, attempt, std::move(result));
                });
            };
            download->stream_cancel = CancelSource{};
            download->stream_active = true;
            download->stream_result.reset();
            if (Result<void> started = http.stream(std::move(request), std::move(callbacks), download->stream_cancel.token());
                !started) {
                download->stream_active = false;
                finish(download, std::unexpected(std::move(started.error())));
            }
        }

        void stream_ended(u64 id, u32 attempt, Result<ports::HttpStatus> result) {
            const DownloadPtr download = find(id);
            if (!download || !download->stream_active) return;
            {
                const std::scoped_lock lock(download->pipe->mutex);
                if (attempt != download->pipe->attempt) return;
            }
            download->stream_active = false;
            download->stream_result = std::move(result);
            settle(download);
        }

        void writer_report(u64 id, u64 written, bool idle) {
            const DownloadPtr download = find(id);
            if (!download) return;
            if (!download->closing) {
                download->done_bytes = written;
                {
                    const std::scoped_lock lock(download->pipe->mutex);
                    if (download->pipe->accepted) download->response_total = download->pipe->total;
                }
                schedule_progress(download);
            }
            if (idle) settle(download);
        }

        // Decides an attempt once its stream ended and the writer drained what it received.
        void settle(const DownloadPtr& download) {
            if (download->stream_active || download->closing) return;
            Pipe& pipe = *download->pipe;
            if (!download->stream_result) {
                bool writer_busy = false;
                {
                    const std::scoped_lock lock(pipe.mutex);
                    writer_busy = pipe.writer_busy;
                }
                if (download->cancelled && download->attempts_used > 0 && !writer_busy)
                    finish(download, std::unexpected(cancelled_diag(*download)));
                return;
            }
            Issue issue = Issue::None;
            bool accepted = false;
            bool backlog = false;
            std::optional<DownloadError> write_error;
            u32 status = 0;
            u64 written = 0;
            std::optional<u64> total;
            std::string validator;
            {
                const std::scoped_lock lock(pipe.mutex);
                if (pipe.writer_busy) return;
                issue = pipe.issue;
                accepted = pipe.accepted;
                backlog = pipe.backlog_exceeded;
                write_error = pipe.write_error;
                status = pipe.status;
                written = pipe.written;
                total = pipe.total;
                validator = pipe.validator;
                // Later callbacks of this attempt are stale from here on.
                ++pipe.attempt;
            }
            Result<ports::HttpStatus> result = std::move(*download->stream_result);
            download->stream_result.reset();

            if (download->cancelled) return finish(download, std::unexpected(cancelled_diag(*download)));
            if (write_error) return finish(download, std::unexpected(to_diagnostic(*write_error)));
            if (accepted) {
                download->validator = validator;
                download->total = total;
                if (download->attempt_start > 0) download->resumed = true;
                if (written > download->attempt_start) download->consecutive_failures = 0;
            }
            if (accepted && backlog) {
                // A disk that wrote nothing meanwhile is not keeping up; that resume costs an attempt.
                if (written > download->attempt_start) return begin_attempt(download, false);
                if (!result) download->last_error = std::move(result.error());
                return retry_later(download);
            }

            switch (issue) {
                case Issue::RangeNotHonored: {
                    download->last_error = download_diag(*download, DownloadErrorCode::RangeNotHonored, download->attempt_start);
                    if (++download->range_failures >= std::max<u32>(download->request.retry.restart_after_range_failures, 1)) {
                        download->range_failures = 0;
                        discard(*download);
                    }
                    return retry_later(download);
                }
                case Issue::SourceChanged:
                    download->last_error = download_diag(*download, DownloadErrorCode::SourceChanged, download->attempt_start);
                    discard(*download);
                    return begin_attempt(download, true);
                case Issue::HttpStatus: {
                    DownloadError error;
                    error.code = DownloadErrorCode::HttpStatus;
                    error.host = download->host;
                    error.file = download->request.file;
                    error.offset = download->attempt_start;
                    error.status = status;
                    download->last_error = to_diagnostic(error);
                    if (retryable_status(status)) return retry_later(download);
                    return finish(download, std::unexpected(*download->last_error));
                }
                case Issue::SizeMismatch: {
                    DownloadError error;
                    error.code = DownloadErrorCode::SizeMismatch;
                    error.host = download->host;
                    error.file = download->request.file;
                    error.offset = total.value_or(0);
                    error.expected_bytes = download->request.expected_size;
                    return finish(download, std::unexpected(to_diagnostic(error)));
                }
                case Issue::None: break;
            }

            if (!result) {
                if (!result.error().retryable) return finish(download, std::unexpected(std::move(result.error())));
                download->last_error = std::move(result.error());
                return retry_later(download);
            }
            download->range_failures = 0;
            // Without a stated size the catalog's is the one to reach.
            const std::optional<u64> wanted = download->total ? download->total : download->request.expected_size;
            if (wanted && written != *wanted) {
                DownloadError error;
                error.code = DownloadErrorCode::SizeMismatch;
                error.host = download->host;
                error.file = download->request.file;
                error.offset = written;
                error.expected_bytes = wanted;
                // A short body resumes; a longer one cannot become right.
                if (written > *wanted) return finish(download, std::unexpected(to_diagnostic(error)));
                download->last_error = to_diagnostic(error);
                return retry_later(download);
            }
            download->done_bytes = written;
            succeed(download);
        }

        // The next attempt starts from byte 0 under a fresh validator.
        static void discard(Download& download) {
            download.validator.clear();
            download.total.reset();
            download.resumed = false;
        }

        void retry_later(const DownloadPtr& download) {
            if (download->attempts_used >= std::max<u32>(download->request.retry.max_attempts, 1)) return exhausted(download);
            const DownloadRetryPolicy& policy = download->request.retry;
            const u32 doublings = std::min<u32>(download->consecutive_failures++, 20);
            const auto delay = std::min(policy.max_delay, policy.first_delay * (i64{1} << doublings));
            download->retry_timer = timers.after(delay, [weak = weak_from_this(), id = download->id] {
                const std::shared_ptr<Core> self = weak.lock();
                if (!self) return;
                if (const DownloadPtr live = self->find(id)) self->begin_attempt(live, true);
            });
        }

        void exhausted(const DownloadPtr& download) {
            DownloadError error;
            error.code = DownloadErrorCode::AttemptsExhausted;
            error.host = download->host;
            error.file = download->request.file;
            error.attempts = download->attempts_used;
            error.cause = download->last_error;
            finish(download, std::unexpected(to_diagnostic(error)));
        }

        [[nodiscard]] Diagnostic download_diag(const Download& download, DownloadErrorCode code, u64 offset) const {
            DownloadError error;
            error.code = code;
            error.host = download.host;
            error.file = download.request.file;
            error.offset = offset;
            return to_diagnostic(error);
        }

        [[nodiscard]] Diagnostic cancelled_diag(const Download& download) const {
            return download_diag(download, DownloadErrorCode::Cancelled, download.done_bytes);
        }

        void cancel(u64 id) {
            const DownloadPtr download = find(id);
            if (!download || download->cancelled) return;
            download->cancelled = true;
            download->retry_timer.cancel();
            download->stream_cancel.cancel(CancelReason::User);
            // Otherwise the stream's end, the writer going idle or the preparation finishes it.
            settle(download);
        }

        void succeed(const DownloadPtr& download) {
            close_then(download, true, [download](Core& self) {
                self.emit_progress(*download);
                DownloadResult result{download->request.file, download->done_bytes, download->resumed};
                self.complete(download, std::move(result));
            });
        }

        void finish(const DownloadPtr& download, Result<DownloadResult> result) {
            if (!result) {
                close_then(download, false, [download, error = std::move(result.error())](Core& self) mutable {
                    self.complete(download, std::unexpected(std::move(error)));
                });
                return;
            }
            complete(download, std::move(result));
        }

        // Closes the file on a worker, and on success drops the sidecar, before `then` runs here.
        template <class Then>
        void close_then(const DownloadPtr& download, bool remove_sidecar, Then then) {
            if (download->closing) return;
            download->closing = true;
            download->retry_timer.cancel();
            download->progress_timer.cancel();
            download->user_cancel.reset();
            workers.submit<bool>(
                [pipe = download->pipe, remove_sidecar](CancelToken) -> Result<bool> {
                    pipe->out.close();
                    if (remove_sidecar) (void)pipe->fs.remove_tree(pipe->sidecar);
                    return true;
                },
                CancelToken{}, strand,
                [weak = weak_from_this(), then = std::move(then)](Result<bool>) mutable {
                    if (const std::shared_ptr<Core> self = weak.lock()) then(*self);
                });
        }

        void complete(const DownloadPtr& download, Result<DownloadResult> result) {
            downloads.erase(download->id);
            download->pipe->report = nullptr;
            UniqueFunction<void(Result<DownloadResult>)> done = std::move(download->done);
            if (done) done(std::move(result));
        }

        void schedule_progress(const DownloadPtr& download) {
            const SteadyTime now = clock.steady_now();
            if (!download->last_emit || now - *download->last_emit >= kProgressInterval) {
                emit_progress(*download);
                return;
            }
            if (download->progress_timer.active()) return;
            download->progress_timer = timers.at(*download->last_emit + kProgressInterval, [weak = weak_from_this(), id = download->id] {
                const std::shared_ptr<Core> self = weak.lock();
                if (!self) return;
                if (const DownloadPtr live = self->find(id); live && !live->closing) self->emit_progress(*live);
            });
        }

        void emit_progress(Download& download) {
            const std::optional<u64> total =
                download.total ? download.total : download.response_total ? download.response_total : download.request.expected_size;
            if (download.last_emitted == std::pair{download.done_bytes, total}) return;
            const SteadyTime now = clock.steady_now();
            download.samples.emplace_back(now, download.done_bytes);
            while (download.samples.size() > 2 && now - download.samples[1].first >= kRateWindow) download.samples.pop_front();
            DownloadProgress progress;
            progress.done = download.done_bytes;
            progress.total = total;
            const auto& [first_time, first_done] = download.samples.front();
            const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - first_time).count();
            if (elapsed > 0 && download.done_bytes >= first_done)
                progress.bytes_per_s = (download.done_bytes - first_done) * 1000 / static_cast<u64>(elapsed);
            download.last_emit = now;
            download.last_emitted = std::pair{download.done_bytes, total};
            if (download.on_progress) download.on_progress(progress);
        }

        HttpClient& http;
        ports::IDiskInfo& disk;
        ports::IFileSystem& fs;
        WorkerPool& workers;
        Executor& strand;
        TimerService& timers;
        IClock& clock;
        u64 next_id = 1;
        std::map<u64, DownloadPtr> downloads;
    };

    Impl(HttpClient& http, ports::IDiskInfo& disk, ports::IFileSystem& fs, WorkerPool& workers, Executor& strand,
         TimerService& timers, IClock& clock)
        : core(std::make_shared<Core>(http, disk, fs, workers, strand, timers, clock)) {}

    ~Impl() {
        for (auto& [id, download] : core->downloads) {
            download->user_cancel.reset();
            download->retry_timer.cancel();
            download->stream_cancel.cancel(CancelReason::Shutdown);
            download->pipe->report = nullptr;
        }
        core->downloads.clear();
    }

    std::shared_ptr<Core> core;
};

ResumableDownloader::ResumableDownloader(HttpClient& http, ports::IDiskInfo& disk, ports::IFileSystem& fs,
                                         WorkerPool& workers, Executor& strand, TimerService& timers, IClock& clock)
    : impl_(std::make_unique<Impl>(http, disk, fs, workers, strand, timers, clock)) {}

ResumableDownloader::~ResumableDownloader() = default;

Result<void> ResumableDownloader::start(DownloadRequest request, CancelToken token,
                                        UniqueFunction<void(const DownloadProgress&)> on_progress,
                                        UniqueFunction<void(Result<DownloadResult>)> done) {
    const std::optional<ParsedUrl> url = parse_url(request.url);
    if (!url) return std::unexpected(to_diagnostic(HttpError{.code = HttpErrorCode::InvalidUrl, .host = request.url}));
    std::error_code error;
    const NativePath parent = request.file.parent_path();
    if (request.file.filename().empty() || parent.empty() || !std::filesystem::is_directory(parent, error)) {
        DownloadError failure;
        failure.code = DownloadErrorCode::Io;
        failure.host = url->host;
        failure.file = request.file;
        return std::unexpected(to_diagnostic(failure));
    }

    Impl::Core& core = *impl_->core;
    auto download = std::make_shared<Impl::Download>();
    download->id = core.next_id++;
    download->host = url->host;
    download->pipe = std::make_shared<Pipe>(core.fs, core.disk, core.workers, request.file, request.url);
    download->pipe->strand = &core.strand;
    download->pipe->report = [weak = core.weak_from_this(), id = download->id](u64 written, bool idle) {
        if (const std::shared_ptr<Impl::Core> self = weak.lock()) self->writer_report(id, written, idle);
    };
    download->request = std::move(request);
    download->on_progress = std::move(on_progress);
    download->done = std::move(done);
    core.downloads.emplace(download->id, download);
    download->user_cancel = token.on_cancel([weak = core.weak_from_this(), id = download->id](CancelReason) {
        if (const std::shared_ptr<Impl::Core> self = weak.lock())
            self->strand.post([weak, id] {
                if (const std::shared_ptr<Impl::Core> owner = weak.lock()) owner->cancel(id);
            });
    });
    core.prepare(download);
    return {};
}

}  // namespace rb::net
