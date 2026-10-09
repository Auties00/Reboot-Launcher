#include "reboot/storage/frontend_state_store.hpp"

#include <algorithm>
#include <deque>
#include <optional>
#include <string>
#include <utility>
#include <variant>

#include "flush_waiters.hpp"
#include "json_text.hpp"
#include "messages.hpp"
#include "reboot/foundation/executor.hpp"
#include "reboot/foundation/flat_map.hpp"
#include "reboot/foundation/text.hpp"
#include "reboot/ports/file_system.hpp"

namespace rb::storage {

namespace {

[[nodiscard]] std::string document_name(const ShellName& shell) { return "frontend/" + shell.value; }

}  // namespace

struct FrontendStateStore::Impl {
    Impl(ports::IFileSystem& file_system, WorkerPool& worker_pool, Executor& strand_executor, NativePath directory,
         StorageMode storage_mode)
        : fs(file_system),
          workers(worker_pool),
          strand(strand_executor),
          dir(std::move(directory)),
          mode(storage_mode),
          puts(strand_executor, "frontend"),
          flushes(strand_executor, "frontend") {}

    struct PendingWrite {
        ShellName shell;
        std::vector<u8> blob;
        u64 waiter = 0;
    };

    ports::IFileSystem& fs;
    WorkerPool& workers;
    Executor& strand;
    NativePath dir;
    StorageMode mode;
    // What each shell's file holds once the queued writes land.
    FlatMap<std::string, std::vector<u8>> cache;
    // One write at a time, so two puts for a shell land in order.
    std::deque<PendingWrite> writes;
    bool writing = false;
    // The first write that failed since the queue was last empty, for the flushes waiting on it.
    std::optional<Diagnostic> failure;
    // A cancelled put ends only its wait; the write still goes out, so the cache stays true.
    FlushWaiters puts;
    FlushWaiters flushes;
    // Cancelled on destruction, so replies that arrive later never touch this object.
    CancelSource alive;

    [[nodiscard]] NativePath file(const ShellName& shell) const { return dir / (shell.value + ".json"); }

    void reply(UniqueFunction<void(Result<void>)> done, Result<void> result) {
        strand.post([done = std::move(done), result = std::move(result)]() mutable { done(std::move(result)); });
    }

    void write_next();
};

void FrontendStateStore::Impl::write_next() {
    if (writes.empty()) {
        writing = false;
        const std::optional<Diagnostic> failed = std::exchange(failure, std::nullopt);
        flushes.finish(failed ? Result<void>(std::unexpected(*failed)) : Result<void>{});
        return;
    }
    writing = true;
    PendingWrite next = std::move(writes.front());
    writes.pop_front();
    workers.submit<std::monostate>(
        [&fs = fs, path = file(next.shell), blob = std::move(next.blob),
         document = document_name(next.shell)](CancelToken) -> Result<std::monostate> {
            if (Result<void> written = fs.atomic_replace(path, blob, false); !written)
                return make_diag(ErrorDomain::Storage, msg::kWriteFailed)
                    .arg("document", document)
                    .arg("path", path)
                    .cause(std::move(written.error()))
                    .fail();
            return std::monostate{};
        },
        CancelToken{}, strand,
        [this, alive_token = alive.token(), shell = next.shell.value, waiter = next.waiter](
            Result<std::monostate> written) {
            if (alive_token.cancelled()) return;
            if (written) {
                puts.finish_one(waiter, {});
            } else {
                // Unless a newer put for the shell is queued, gets must see what the disk still holds.
                const bool newer = std::ranges::any_of(writes, [&shell](const PendingWrite& queued) {
                    return queued.shell.value == shell;
                });
                if (!newer) cache.erase(shell);
                if (!failure) failure = written.error();
                puts.finish_one(waiter, std::unexpected(std::move(written.error())));
            }
            write_next();
        });
}

FrontendStateStore::FrontendStateStore(ports::IFileSystem& fs, WorkerPool& workers, Executor& strand, NativePath dir,
                                       StorageMode mode)
    : impl_(std::make_unique<Impl>(fs, workers, strand, std::move(dir), mode)) {}

FrontendStateStore::~FrontendStateStore() { impl_->alive.cancel(CancelReason::Shutdown); }

void FrontendStateStore::get(const ShellName& shell, CancelToken cancel,
                             UniqueFunction<void(Result<std::vector<u8>>)> done) {
    Impl& impl = *impl_;
    const auto cached = impl.cache.find(shell.value);
    if (cached != impl.cache.end() || impl.mode == StorageMode::InMemory) {
        std::vector<u8> blob = cached != impl.cache.end() ? cached->second : std::vector<u8>{};
        impl.strand.post([done = std::move(done), blob = std::move(blob)]() mutable { done(std::move(blob)); });
        return;
    }
    impl.workers.submit<std::vector<u8>>(
        [&fs = impl.fs, path = impl.file(shell), document = document_name(shell)](
            CancelToken token) -> Result<std::vector<u8>> {
            if (token.cancelled()) return std::unexpected(cancelled(document));
            Result<std::vector<u8>> blob = fs.read_all(path);
            if (!blob && blob.error().kind == ErrorKind::NotFound) return std::vector<u8>{};
            return blob;
        },
        std::move(cancel), impl.strand,
        [&impl, alive_token = impl.alive.token(), shell = shell.value,
         done = std::move(done)](Result<std::vector<u8>> blob) mutable {
            if (alive_token.cancelled()) return;
            // A put that landed while this read ran is newer than the file.
            if (const auto newer = impl.cache.find(shell); newer != impl.cache.end()) blob = newer->second;
            else if (blob) impl.cache.emplace(shell, *blob);
            done(std::move(blob));
        });
}

void FrontendStateStore::put(const ShellName& shell, std::vector<u8> blob, CancelToken cancel,
                             UniqueFunction<void(Result<void>)> done) {
    Impl& impl = *impl_;
    if (blob.size() > kFrontendStateMaxBytes) {
        impl.reply(std::move(done), invalid_input(msg::kFrontendStateTooLarge)
                                        .arg("shell", shell.value)
                                        .arg("size", blob.size())
                                        .arg("limit", kFrontendStateMaxBytes)
                                        .fail());
        return;
    }
    const std::string_view text = as_text(blob);
    if (!is_valid_utf8(text) || !parse_json(text)) {
        impl.reply(std::move(done), invalid_input(msg::kFrontendStateNotJson).arg("shell", shell.value).fail());
        return;
    }
    impl.cache.insert_or_assign(shell.value, blob);
    if (impl.mode == StorageMode::InMemory) {
        impl.reply(std::move(done), {});
        return;
    }
    const u64 waiter = impl.puts.add(cancel, std::move(done));
    impl.writes.push_back(Impl::PendingWrite{shell, std::move(blob), waiter});
    if (!impl.writing) impl.write_next();
}

void FrontendStateStore::flush(CancelToken cancel, UniqueFunction<void(Result<void>)> done) {
    Impl& impl = *impl_;
    if (!impl.writing) {
        impl.reply(std::move(done), {});
        return;
    }
    impl.flushes.add(cancel, std::move(done));
}

}  // namespace rb::storage
