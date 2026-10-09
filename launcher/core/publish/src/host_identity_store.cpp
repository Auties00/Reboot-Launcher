#include "reboot/publish/host_identity_store.hpp"

#include <deque>
#include <iterator>
#include <map>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include "identity_file.hpp"
#include "messages.hpp"
#include "reboot/foundation/executor.hpp"
#include "reboot/foundation/flat_map.hpp"
#include "reboot/foundation/log.hpp"
#include "reboot/foundation/random.hpp"
#include "reboot/ports/file_system.hpp"

namespace reboot::publish {

namespace {

using Done = UniqueFunction<void(Result<void>)>;

[[nodiscard]] NativePath identity_path(const NativePath& dir, const HostProfileId& profile) {
    return dir / (format_uuid(profile.value) + ".json");
}

[[nodiscard]] std::span<const u8> text_bytes(const SecretString& text) noexcept {
    return {reinterpret_cast<const u8*>(text.reveal().data()), text.reveal().size()};
}

void wipe(std::vector<u8>& bytes) noexcept { secure_wipe(bytes.data(), bytes.size()); }

void mask(const HostToken& token) { Logger::redactor().add_secret(text_bytes(token_text(token))); }

void unmask(const std::optional<HostToken>& token) {
    if (token) Logger::redactor().remove_secret(text_bytes(token_text(*token)));
}

[[nodiscard]] Diagnostic profile_diag(MessageId id, const HostProfileId& profile, ErrorKind kind) {
    return make_diag(ErrorDomain::Publish, id).kind(kind).arg("profile", format_uuid(profile.value)).build();
}

}  // namespace

struct HostIdentityStore::Impl {
    struct Alive {
        Impl* impl = nullptr;
    };

    struct Entry {
        HostIdentity identity;
        bool held = false;
    };

    enum class JobKind : u8 { Write, Remove, Flush };

    // Disk work runs one job at a time, so a write and a later remove of one file stay in order.
    struct Job {
        JobKind kind = JobKind::Write;
        HostProfileId profile;
        SecretBytes bytes;
        std::vector<Done> waiters;
    };

    Impl(ports::IFileSystem& fs_in, WorkerPool& workers_in, Executor& strand_in, IRandom& random_in, OpRegistry& ops_in,
         NativePath dir_in)
        : fs(fs_in), workers(workers_in), strand(strand_in), random(random_in), ops(ops_in), dir(std::move(dir_in)),
          alive(std::make_shared<Alive>(Alive{this})) {}

    // Tokens stay masked: the logger may still write queued lines after the store is gone.
    ~Impl() { alive->impl = nullptr; }

    Impl(const Impl&) = delete;
    Impl& operator=(const Impl&) = delete;

    [[nodiscard]] Diagnostic memory_only_error() const {
        return make_diag(ErrorDomain::Publish, msg::kIdentityDirUnavailable).arg("path", dir).build();
    }

    void post_result(Done done, Result<void> result) {
        if (!done) return;
        strand.post([done = std::move(done), result = std::move(result)]() mutable { done(std::move(result)); });
    }

    Entry& mint(const HostProfileId& profile) {
        Entry& entry = entries[profile];
        entry.identity.profile = profile;
        entry.identity.server = ServerId{uuid_v4(random)};
        entry.identity.token.reset();
        persist(profile, nullptr);
        return entry;
    }

    void replace_token(Entry& entry, std::optional<HostToken> token) {
        unmask(entry.identity.token);
        entry.identity.token = std::move(token);
        if (entry.identity.token) mask(*entry.identity.token);
    }

    void persist(const HostProfileId& profile, Done waiter) {
        if (memory_only) {
            post_result(std::move(waiter), std::unexpected(memory_only_error()));
            return;
        }
        const auto it = entries.find(profile);
        if (it == entries.end()) {
            post_result(std::move(waiter), {});
            return;
        }
        SecretBytes bytes = encode_identity(it->second.identity.server, it->second.identity.token);
        // A queued write of the same file takes the newer bytes, unless another job of it comes after.
        const std::size_t first_queued = busy ? 1 : 0;
        for (std::size_t i = queue.size(); i > first_queued; --i) {
            Job& job = queue[i - 1];
            if (job.profile != profile || job.kind == JobKind::Flush) continue;
            if (job.kind != JobKind::Write) break;
            job.bytes = std::move(bytes);
            if (waiter) job.waiters.push_back(std::move(waiter));
            return;
        }
        Job job{JobKind::Write, profile, std::move(bytes), {}};
        if (waiter) job.waiters.push_back(std::move(waiter));
        enqueue(std::move(job));
    }

    // Ahead of pending flushes, so a flush also covers the writes its earlier jobs' waiters cause.
    void enqueue(Job job) {
        auto at = queue.end();
        const auto first_queued = queue.begin() + (busy ? 1 : 0);
        while (at != first_queued && std::prev(at)->kind == JobKind::Flush) --at;
        queue.insert(at, std::move(job));
        pump();
    }

    void pump() {
        while (!busy && !queue.empty()) {
            Job& job = queue.front();
            if (job.kind == JobKind::Flush) {
                Result<void> result = failed.empty() ? Result<void>{} : Result<void>(std::unexpected(*last_failure));
                std::vector<Done> waiters = std::move(job.waiters);
                queue.pop_front();
                for (Done& waiter : waiters) post_result(std::move(waiter), result);
                continue;
            }
            busy = true;
            const NativePath path = identity_path(dir, job.profile);
            const JobKind kind = job.kind;
            UniqueFunction<Result<void>(CancelToken)> work;
            if (kind == JobKind::Write) {
                work = [&fs = fs, path, bytes = std::move(job.bytes)](CancelToken) -> Result<void> {
                    if (Result<void> written = fs.atomic_replace(path, bytes.reveal(), false); !written) return written;
                    return fs.restrict_to_owner(path);
                };
            } else {
                work = [&fs = fs, path](CancelToken) -> Result<void> { return fs.remove_tree(path); };
            }
            workers.submit<void>(std::move(work), CancelToken{}, strand, [weak = alive](Result<void> result) {
                if (Impl* self = weak->impl) self->on_job_done(std::move(result));
            });
        }
    }

    void on_job_done(Result<void> result) {
        Job job = std::move(queue.front());
        queue.pop_front();
        busy = false;
        if (result) {
            failed.erase(job.profile);
        } else if (job.kind == JobKind::Write) {
            failed.insert(job.profile);
            last_failure = result.error();
            REBOOT_LOG_WARN(Host, "The server identity of host profile {} could not be saved ({})",
                            format_uuid(job.profile.value), result.error().id);
        }
        for (Done& waiter : job.waiters) waiter(result);
        pump();
    }

    void finish_import(Operation<ServerId>& op, const HostProfileId& profile, Result<StoredIdentity> loaded) {
        // A cancelled or timed-out import leaves the identity as it was.
        if (op.done()) {
            op.complete(Cancelled{op.token().reason().value_or(CancelReason::User)});
            return;
        }
        if (!loaded) {
            op.complete(Failed{std::move(loaded.error())});
            return;
        }
        const auto it = entries.find(profile);
        if (it != entries.end() && it->second.held) {
            op.complete(Failed{profile_diag(msg::kIdentityInUse, profile, ErrorKind::Conflict)});
            return;
        }
        std::optional<StoredIdentity> previous;
        if (it != entries.end()) {
            previous.emplace(StoredIdentity{it->second.identity.server, std::nullopt});
            if (it->second.identity.token) previous->token.emplace(it->second.identity.token->reveal());
        }
        Entry& entry = entries[profile];
        entry.identity.profile = profile;
        entry.identity.server = loaded->server;
        replace_token(entry, std::move(loaded->token));
        // Held by the import until its file is written, so nothing publishes an identity that may be undone.
        entry.held = true;
        const ServerId server = loaded->server;
        persist(profile, [weak = alive, &op, profile, server, previous = std::move(previous)](Result<void> saved) mutable {
            Impl* self = weak->impl;
            if (self == nullptr) {
                op.complete(Cancelled{CancelReason::Shutdown});
                return;
            }
            if (const auto held = self->entries.find(profile); held != self->entries.end()) held->second.held = false;
            if (saved && !op.done()) {
                op.complete(Completed<ServerId>{server});
                return;
            }
            // A failed or cancelled import leaves the identity as it was.
            self->restore(profile, std::move(previous));
            if (!saved) op.complete(Failed{std::move(saved.error())});
            else op.complete(Cancelled{op.token().reason().value_or(CancelReason::User)});
        });
    }

    void restore(const HostProfileId& profile, std::optional<StoredIdentity> previous) {
        if (!previous) {
            if (const auto it = entries.find(profile); it != entries.end()) {
                unmask(it->second.identity.token);
                entries.erase(it);
            }
            remove_file(profile, nullptr);
            return;
        }
        Entry& entry = entries[profile];
        entry.identity.profile = profile;
        entry.identity.server = previous->server;
        replace_token(entry, std::move(previous->token));
        persist(profile, nullptr);
    }

    void remove_file(const HostProfileId& profile, Done removed) {
        failed.erase(profile);
        if (memory_only) {
            post_result(std::move(removed), {});
            return;
        }
        Job job{JobKind::Remove, profile, {}, {}};
        if (removed) job.waiters.push_back(std::move(removed));
        enqueue(std::move(job));
    }

    ports::IFileSystem& fs;
    WorkerPool& workers;
    Executor& strand;
    IRandom& random;
    OpRegistry& ops;
    NativePath dir;
    std::shared_ptr<Alive> alive;

    bool memory_only = false;
    std::map<HostProfileId, Entry> entries;
    std::deque<Job> queue;
    bool busy = false;
    // Profiles whose last write failed; flush() writes them again.
    FlatSet<HostProfileId> failed;
    std::optional<Diagnostic> last_failure;
};

HostIdentityStore::HostIdentityStore(ports::IFileSystem& fs, WorkerPool& workers, Executor& strand, IRandom& random,
                                     OpRegistry& ops, NativePath dir)
    : impl_(std::make_unique<Impl>(fs, workers, strand, random, ops, std::move(dir))) {}

HostIdentityStore::~HostIdentityStore() = default;

Result<std::vector<IdentityLoadIssue>> HostIdentityStore::load(std::span<const HostProfileId> profiles) {
    Impl& impl = *impl_;
    if (Result<void> made = impl.fs.create_dirs_owner_only(impl.dir); !made)
        return make_diag(ErrorDomain::Publish, msg::kIdentityDirUnavailable)
            .arg("path", impl.dir)
            .cause(std::move(made.error()))
            .fail();

    std::vector<IdentityLoadIssue> issues;
    for (const HostProfileId& profile : profiles) {
        const NativePath path = identity_path(impl.dir, profile);
        Result<std::vector<u8>> bytes = impl.fs.read_all(path);
        if (!bytes) {
            if (bytes.error().kind == ErrorKind::NotFound) continue;
            issues.push_back(IdentityLoadIssue{
                profile,
                make_diag(ErrorDomain::Publish, msg::kIdentityUnreadable)
                    .severity(Severity::Warning)
                    .arg("profile", format_uuid(profile.value))
                    .cause(std::move(bytes.error()))
                    .build(),
                std::nullopt});
            continue;
        }
        std::optional<StoredIdentity> stored = decode_identity(*bytes);
        if (stored) {
            Impl::Entry& entry = impl.entries[profile];
            entry.identity.profile = profile;
            entry.identity.server = stored->server;
            impl.replace_token(entry, std::move(stored->token));
            wipe(*bytes);
            continue;
        }
        // Moved aside rather than overwritten, so nothing in it is lost.
        NativePath aside = path;
        aside += ".corrupt-" + random_token_hex(impl.random, 4);
        std::optional<NativePath> quarantined_to;
        if (impl.fs.atomic_replace(aside, *bytes, false) && impl.fs.restrict_to_owner(aside) && impl.fs.remove_tree(path))
            quarantined_to = std::move(aside);
        wipe(*bytes);
        issues.push_back(IdentityLoadIssue{profile,
                                           make_diag(ErrorDomain::Publish, msg::kIdentityUnreadable)
                                               .severity(Severity::Warning)
                                               .arg("profile", format_uuid(profile.value))
                                               .build(),
                                           std::move(quarantined_to)});
    }
    return issues;
}

void HostIdentityStore::load_memory_only() { impl_->memory_only = true; }

std::optional<ServerId> HostIdentityStore::server_id(const HostProfileId& profile) const {
    const auto it = impl_->entries.find(profile);
    if (it == impl_->entries.end()) return std::nullopt;
    return it->second.identity.server;
}

Result<ServerId> HostIdentityStore::ensure(const HostProfileId& profile) {
    if (const auto it = impl_->entries.find(profile); it != impl_->entries.end()) return it->second.identity.server;
    return impl_->mint(profile).identity.server;
}

Result<IdentityHold> HostIdentityStore::acquire(const HostProfileId& profile) {
    if (const auto it = impl_->entries.find(profile); it != impl_->entries.end() && it->second.held)
        return std::unexpected(profile_diag(msg::kProfileBusy, profile, ErrorKind::Conflict));
    if (Result<ServerId> ensured = ensure(profile); !ensured) return std::unexpected(std::move(ensured.error()));
    impl_->entries.at(profile).held = true;
    return IdentityHold(*this, profile);
}

void HostIdentityStore::save_token(const IdentityHold& hold, HostToken token, UniqueFunction<void(Result<void>)> saved) {
    const auto it = impl_->entries.find(hold.profile());
    if (!hold.held() || it == impl_->entries.end()) {
        impl_->post_result(std::move(saved), std::unexpected(internal_bug("HostIdentityStore::save_token")));
        return;
    }
    impl_->replace_token(it->second, std::move(token));
    impl_->persist(hold.profile(), std::move(saved));
}

ServerId HostIdentityStore::rotate(const IdentityHold& hold, UniqueFunction<void(Result<void>)> saved) {
    Impl::Entry& entry = impl_->entries[hold.profile()];
    entry.identity.profile = hold.profile();
    impl_->replace_token(entry, std::nullopt);
    entry.identity.server = ServerId{uuid_v4(impl_->random)};
    const ServerId server = entry.identity.server;
    impl_->persist(hold.profile(), std::move(saved));
    return server;
}

Result<void> HostIdentityStore::remove(const HostProfileId& profile, UniqueFunction<void(Result<void>)> removed) {
    Impl& impl = *impl_;
    if (const auto it = impl.entries.find(profile); it != impl.entries.end()) {
        if (it->second.held) return std::unexpected(profile_diag(msg::kIdentityInUse, profile, ErrorKind::Conflict));
        unmask(it->second.identity.token);
        impl.entries.erase(it);
    }
    impl.remove_file(profile, std::move(removed));
    return {};
}

Result<OpHandle> HostIdentityStore::start_export(const HostProfileId& profile, NativePath destination,
                                                 DisconnectPolicy policy) {
    Impl& impl = *impl_;
    const auto it = impl.entries.find(profile);
    if (it == impl.entries.end() || !it->second.identity.token)
        return std::unexpected(profile_diag(msg::kIdentityNotRegistered, profile, ErrorKind::NotFound));
    SecretBytes bytes = encode_identity(it->second.identity.server, it->second.identity.token);

    auto created = impl.ops.create<void>(OpKind::Generic, policy, std::nullopt);
    Operation<void>& op = created.second;
    // False when the op was cancelled before the work began.
    impl.workers.submit<bool>(
        [&fs = impl.fs, destination = std::move(destination), bytes = std::move(bytes)](CancelToken token) -> Result<bool> {
            if (token.cancelled()) return false;
            Result<ports::FileRevision> existing = fs.revision(destination);
            if (existing)
                return make_diag(ErrorDomain::Publish, msg::kExportDestinationExists)
                    .kind(ErrorKind::Conflict)
                    .arg("path", destination)
                    .fail();
            if (existing.error().kind != ErrorKind::NotFound) return std::unexpected(std::move(existing.error()));
            if (Result<void> made = fs.create_dirs_owner_only(destination); !made) return std::unexpected(std::move(made.error()));
            const NativePath file = destination / kIdentityExportFile;
            if (Result<void> written = fs.atomic_replace(file, bytes.reveal(), false); !written)
                return std::unexpected(std::move(written.error()));
            if (Result<void> restricted = fs.restrict_to_owner(file); !restricted)
                return std::unexpected(std::move(restricted.error()));
            return true;
        },
        op.token(), impl.strand, [&op](Result<bool> result) {
            if (!result) op.complete(Failed{std::move(result.error())});
            else if (!*result) op.complete(Cancelled{op.token().reason().value_or(CancelReason::User)});
            else op.complete(Completed<void>{});
        });
    return created.first;
}

Result<OpHandle> HostIdentityStore::start_import(const HostProfileId& profile, NativePath source, DisconnectPolicy policy) {
    Impl& impl = *impl_;
    if (const auto it = impl.entries.find(profile); it != impl.entries.end() && it->second.held)
        return std::unexpected(profile_diag(msg::kIdentityInUse, profile, ErrorKind::Conflict));

    auto created = impl.ops.create<ServerId>(OpKind::Import, policy, std::nullopt);
    Operation<ServerId>& op = created.second;
    // nullopt when the op was cancelled before the work began.
    impl.workers.submit<std::optional<StoredIdentity>>(
        [&fs = impl.fs, source = std::move(source)](CancelToken token) -> Result<std::optional<StoredIdentity>> {
            if (token.cancelled()) return std::nullopt;
            const auto invalid = [&] {
                return make_diag(ErrorDomain::Publish, msg::kIdentityFileInvalid).kind(ErrorKind::InvalidInput).arg("path", source);
            };
            Result<std::vector<u8>> bytes = fs.read_all(source / kIdentityExportFile);
            if (!bytes) return invalid().cause(std::move(bytes.error())).fail();
            std::optional<StoredIdentity> stored = decode_identity(*bytes);
            wipe(*bytes);
            if (!stored || !stored->token) return invalid().fail();
            return stored;
        },
        op.token(), impl.strand, [weak = impl.alive, &op, profile](Result<std::optional<StoredIdentity>> loaded) mutable {
            Impl* self = weak->impl;
            if (self == nullptr) {
                op.complete(Cancelled{CancelReason::Shutdown});
                return;
            }
            if (loaded && !*loaded) {
                op.complete(Cancelled{op.token().reason().value_or(CancelReason::User)});
                return;
            }
            self->finish_import(op, profile, loaded ? Result<StoredIdentity>(std::move(**loaded))
                                                    : Result<StoredIdentity>(std::unexpected(std::move(loaded.error()))));
        });
    return created.first;
}

void HostIdentityStore::flush(UniqueFunction<void(Result<void>)> done) {
    Impl& impl = *impl_;
    if (impl.memory_only) {
        impl.post_result(std::move(done), {});
        return;
    }
    const std::vector<HostProfileId> retry(impl.failed.begin(), impl.failed.end());
    for (const HostProfileId& profile : retry) impl.persist(profile, nullptr);
    Impl::Job job{Impl::JobKind::Flush, {}, {}, {}};
    if (done) job.waiters.push_back(std::move(done));
    impl.queue.push_back(std::move(job));
    impl.pump();
}

void HostIdentityStore::release(const HostProfileId& profile) noexcept {
    if (const auto it = impl_->entries.find(profile); it != impl_->entries.end()) it->second.held = false;
}

const HostIdentity& HostIdentityStore::held_identity(const HostProfileId& profile) const {
    static const HostIdentity kNone{};
    const auto it = impl_->entries.find(profile);
    return it == impl_->entries.end() ? kNone : it->second.identity;
}

IdentityHold::IdentityHold(IdentityHold&& other) noexcept
    : store_(std::exchange(other.store_, nullptr)), profile_(other.profile_) {}

IdentityHold& IdentityHold::operator=(IdentityHold&& other) noexcept {
    if (this != &other) {
        if (store_ != nullptr) store_->release(profile_);
        store_ = std::exchange(other.store_, nullptr);
        profile_ = other.profile_;
    }
    return *this;
}

IdentityHold::~IdentityHold() {
    if (store_ != nullptr) store_->release(profile_);
}

const HostIdentity& IdentityHold::identity() const {
    static const HostIdentity kNone{};
    return store_ != nullptr ? store_->held_identity(profile_) : kNone;
}

}  // namespace reboot::publish
