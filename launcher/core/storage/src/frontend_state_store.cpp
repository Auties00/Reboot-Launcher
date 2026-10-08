#include "reboot/storage/frontend_state_store.hpp"

#include <deque>
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

namespace reboot::storage {

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
          waiters(strand_executor, "frontend") {}

    struct PendingWrite {
        ShellName shell;
        std::vector<u8> blob;
        CancelToken cancel;
        UniqueFunction<void(Result<void>)> done;
    };

    ports::IFileSystem& fs;
    WorkerPool& workers;
    Executor& strand;
    NativePath dir;
    StorageMode mode;
    FlatMap<std::string, std::vector<u8>> cache;
    // One write at a time, so two puts for a shell land in order.
    std::deque<PendingWrite> writes;
    bool writing = false;
    FlushWaiters waiters;
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
        waiters.finish({});
        return;
    }
    writing = true;
    PendingWrite next = std::move(writes.front());
    writes.pop_front();
    const std::string document = document_name(next.shell);
    workers.submit<std::monostate>(
        [&fs = fs, path = file(next.shell), blob = std::move(next.blob), document](
            CancelToken cancel) -> Result<std::monostate> {
            if (cancel.cancelled()) return std::unexpected(cancelled(document));
            if (Result<void> written = fs.atomic_replace(path, blob, false); !written)
                return make_diag(ErrorDomain::Storage, msg::kWriteFailed)
                    .arg("document", document)
                    .arg("path", path)
                    .cause(std::move(written.error()))
                    .fail();
            return std::monostate{};
        },
        next.cancel, strand,
        [this, alive_token = alive.token(), done = std::move(next.done)](Result<std::monostate> written) mutable {
            if (alive_token.cancelled()) return;
            if (written) done({});
            else done(std::unexpected(std::move(written.error())));
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
    impl.writes.push_back(Impl::PendingWrite{shell, std::move(blob), std::move(cancel), std::move(done)});
    if (!impl.writing) impl.write_next();
}

void FrontendStateStore::flush(CancelToken cancel, UniqueFunction<void(Result<void>)> done) {
    Impl& impl = *impl_;
    if (!impl.writing) {
        impl.reply(std::move(done), {});
        return;
    }
    impl.waiters.add(cancel, std::move(done));
}

}  // namespace reboot::storage
