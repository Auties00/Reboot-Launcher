#include "reboot/game_channel/ue_log_tail.hpp"

#include <memory>
#include <optional>
#include <utility>
#include <vector>

#include "game_channel_error.hpp"
#include "reboot/foundation/cancel.hpp"
#include "reboot/foundation/executor.hpp"
#include "reboot/foundation/log.hpp"
#include "reboot/foundation/native_path.hpp"
#include "reboot/ports/file_system.hpp"

namespace reboot::game_channel {

namespace {

// One read per poll at most; a longer backlog is read again at once.
constexpr std::size_t kMaxRead = std::size_t{1} << 20;

// Where the tail stands in the file it last saw.
struct TailPosition {
    // False until a baseline was recorded; the first successful read then only records one.
    bool known = false;
    // nullopt while the log does not exist.
    std::optional<u64> file_id;
    u64 offset = 0;
};

struct TailRead {
    TailPosition next;
    std::vector<u8> bytes;
};

// On a worker. A file that is new, or shorter than the offset, was replaced or truncated, so it is
// read again from its start.
[[nodiscard]] Result<TailRead> read_tail(ports::IFileSystem& files, const NativePath& path, TailPosition at) {
    const auto missing = [](const Diagnostic& error) { return error.kind == ErrorKind::NotFound; };
    auto read = files.read_shared(path, at.known ? at.offset : 0, at.known ? kMaxRead : 0);
    if (!read) {
        if (!missing(read.error())) return std::unexpected(std::move(read.error()));
        return TailRead{TailPosition{true, std::nullopt, 0}, {}};
    }
    if (!at.known) return TailRead{TailPosition{true, read->revision.file_id, read->revision.size}, {}};
    if (at.file_id == read->revision.file_id && read->revision.size >= at.offset)
        return TailRead{TailPosition{true, at.file_id, at.offset + read->bytes.size()}, std::move(read->bytes)};

    auto restarted = files.read_shared(path, 0, kMaxRead);
    if (!restarted) {
        if (!missing(restarted.error())) return std::unexpected(std::move(restarted.error()));
        return TailRead{TailPosition{true, std::nullopt, 0}, {}};
    }
    return TailRead{TailPosition{true, restarted->revision.file_id, restarted->bytes.size()}, std::move(restarted->bytes)};
}


// Shared with worker replies and timers, which only hold it weakly.
struct Tail : std::enable_shared_from_this<Tail> {
    Tail(ports::IFileSystem& files_ref, WorkerPool& workers_ref, Executor& strand_ref, TimerService& timers_ref,
         NativePath log_path, UniqueFunction<void(std::span<const u8>)> handler)
        : files(files_ref),
          workers(workers_ref),
          strand(strand_ref),
          timers(timers_ref),
          path(std::move(log_path)),
          on_bytes(std::move(handler)) {}

    void read() {
        workers.submit<TailRead>(
            [&files = files, path = path, at = position](CancelToken token) -> Result<TailRead> {
                if (token.cancelled()) return TailRead{at, {}};
                return read_tail(files, path, at);
            },
            cancel.token(), strand,
            [weak = weak_from_this()](Result<TailRead> result) {
                if (const auto self = weak.lock()) self->apply(std::move(result));
            });
    }

    void apply(Result<TailRead> result) {
        const std::weak_ptr<Tail> weak = weak_from_this();
        if (result) {
            position = result->next;
        } else if (!error_logged) {
            error_logged = true;
            const Diagnostic diag = to_diagnostic(GameChannelError{.code = GameChannelErrorCode::LogUnreadable,
                                                                   .path = path,
                                                                   .os_error = result.error().os_error,
                                                                   .cause = result.error()});
            REBOOT_LOG_WARN(Play, "{} {}: {}", diag.id, display_utf8(path), result.error().id);
        }
        // An unreadable baseline still lets the game launch; the next good read records it.
        if (ready) {
            UniqueFunction<void()> done = std::move(ready);
            done();
            if (weak.expired()) return;
        }
        const bool more = result && result->bytes.size() == kMaxRead;
        if (result && !result->bytes.empty()) {
            UniqueFunction<void(std::span<const u8>)> handler = std::move(on_bytes);
            handler(result->bytes);
            if (weak.expired()) return;
            if (!on_bytes) on_bytes = std::move(handler);
        }
        if (more) {
            read();
            return;
        }
        poll = timers.after(kUeLogPollInterval, [weak] {
            if (const auto self = weak.lock()) self->read();
        });
    }

    ports::IFileSystem& files;
    WorkerPool& workers;
    Executor& strand;
    TimerService& timers;
    NativePath path;
    UniqueFunction<void(std::span<const u8>)> on_bytes;
    UniqueFunction<void()> ready;
    TailPosition position;
    CancelSource cancel;
    TimerHandle poll;
    bool started = false;
    bool error_logged = false;
};

}  // namespace

struct UeLogTail::Impl {
    std::shared_ptr<Tail> tail;
};

UeLogTail::UeLogTail(ports::IFileSystem& files, WorkerPool& workers, Executor& strand, TimerService& timers, NativePath path,
                     UniqueFunction<void(std::span<const u8>)> on_bytes)
    : impl_(std::make_unique<Impl>(
          Impl{std::make_shared<Tail>(files, workers, strand, timers, std::move(path), std::move(on_bytes))})) {}

UeLogTail::~UeLogTail() { impl_->tail->cancel.cancel(CancelReason::Shutdown); }

void UeLogTail::start(UniqueFunction<void()> ready) {
    Tail& tail = *impl_->tail;
    if (tail.started) return;
    tail.started = true;
    tail.ready = std::move(ready);
    tail.read();
}

}  // namespace reboot::game_channel
