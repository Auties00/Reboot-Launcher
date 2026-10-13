#include "reboot/secrets/secret_service.hpp"

#include <algorithm>
#include <any>
#include <charconv>
#include <deque>
#include <expected>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

#include "messages.hpp"
#include "reboot/foundation/events.hpp"
#include "reboot/foundation/executor.hpp"
#include "reboot/foundation/log.hpp"
#include "reboot/foundation/user_request.hpp"
#include "reboot/ports/secret_store.hpp"
#include "reboot/secrets/secret_error.hpp"
#include "reboot/secrets/secret_state_changed_event.hpp"
#include "store_layout.hpp"

namespace rb::secrets {

namespace {

using ports::SecretStoreKind;

// Only UserActionResolved arrives here, so a small queue is enough; an overflow is a Resync.
constexpr std::size_t kResolutionBudget = std::size_t{64} * 1024;

[[nodiscard]] Diagnostic kind_diag(MessageId message, SecretKind kind, ErrorKind error_kind = ErrorKind::Generic) {
    return make_diag(ErrorDomain::Secrets, message).kind(error_kind).arg("kind", kind_name(kind));
}

[[nodiscard]] Diagnostic scoped_diag(MessageId message, const SecretTarget& target, ErrorKind error_kind) {
    return make_diag(ErrorDomain::Secrets, message)
        .kind(error_kind)
        .arg("kind", kind_name(target.kind))
        .arg("scope", target.scope.text());
}

[[nodiscard]] Diagnostic not_ready() { return make_diag(ErrorDomain::Secrets, msg::kNotReady).retryable(); }

[[nodiscard]] Diagnostic timed_out() {
    return make_diag(ErrorDomain::Secrets, msg::kStoreTimedOut).arg("timeout", kStoreCallDeadline).retryable();
}

// A deadline stays a timeout; a store failure is wrapped with what it meant for this kind.
[[nodiscard]] Diagnostic store_failure(MessageId message, SecretKind kind, Diagnostic cause) {
    if (has_error(cause, SecretError::StoreTimedOut)) return cause;
    return make_diag(ErrorDomain::Secrets, message).arg("kind", kind_name(kind)).cause(std::move(cause));
}

[[nodiscard]] SecretBytes copy_of(const SecretBytes& value) { return SecretBytes(std::vector<u8>(value.reveal())); }

[[nodiscard]] SecretLocation location_for(SecretStoreKind kind) {
    return kind == SecretStoreKind::File ? SecretLocation::FileStore : SecretLocation::OsStore;
}

[[nodiscard]] std::optional<RequestId> join_request_of(const SecretScope& scope) {
    const std::string& text = scope.text();
    u64 value = 0;
    const char* end = text.data() + text.size();
    const auto [last, error] = std::from_chars(text.data(), end, value);
    if (error != std::errc{} || last != end) return std::nullopt;
    return RequestId{value};
}

[[nodiscard]] std::string coalesce_key(const SecretTarget& target) {
    std::string key(kind_name(target.kind));
    key += '/';
    key += target.scope.text();
    return key;
}

struct StoreReply {
    SecretStoreKind kind = SecretStoreKind::Unavailable;
    std::vector<std::pair<SecretTarget, SecretBytes>> loaded;
    bool index_written = false;
    // How many index chunks the store may hold now, once the call read or wrote the index.
    std::optional<std::size_t> index_chunks;
    // The loaded index holds lines that the loaded values would not give it.
    bool index_stale = false;
    // A store call failed after the ones before it succeeded.
    std::optional<Diagnostic> failure;
};

using StoreWork = UniqueFunction<StoreReply(ports::ISecretStore&)>;
using StoreDone = UniqueFunction<void(Result<StoreReply>)>;

struct StoreCall {
    StoreWork work;
    StoreDone done;
};

// Runs when the job reaches the front of the store queue; nullopt when no store call is needed.
using StoreJob = UniqueFunction<std::optional<StoreCall>()>;

[[nodiscard]] Diagnostic failure_of(const Result<StoreReply>& reply) {
    return reply ? *reply->failure : reply.error();
}

[[nodiscard]] bool succeeded(const Result<StoreReply>& reply) { return reply && !reply->failure; }

// Erases chunks [from, to) highest first, so the chunks left always start at 0; returns how many may be left.
[[nodiscard]] std::size_t erase_index_chunks(ports::ISecretStore& store, const detail::StoreLayout& layout,
                                             std::size_t from, std::size_t to) {
    for (std::size_t chunk = to; chunk > from; --chunk)
        if (!store.erase(layout.index_key(chunk - 1))) return chunk;
    return from;
}

// Puts every chunk, highest first when the index grew and lowest first when it shrank, so a line
// only moves to a chunk already written and every stored value stays listed; then drops the rest.
void write_index(ports::ISecretStore& store, const detail::StoreLayout& layout,
                 const std::vector<std::vector<u8>>& chunks, bool grew, std::size_t stored_chunks, StoreReply& reply) {
    reply.index_chunks = std::max(stored_chunks, chunks.size());
    for (std::size_t i = 0; i < chunks.size(); ++i) {
        const std::size_t chunk = grew ? chunks.size() - 1 - i : i;
        if (Result<void> put = store.put(layout.index_key(chunk), chunks[chunk]); !put) {
            reply.failure = std::move(put.error());
            return;
        }
    }
    reply.index_written = true;
    reply.index_chunks = erase_index_chunks(store, layout, chunks.size(), stored_chunks);
}

}  // namespace

struct SecretService::Impl {
    enum class Phase : u8 { Idle, Loading, Loaded };

    struct Entry {
        SecretBytes value;
        SecretState state;
        // 0 for a loaded value, which no answer to NeedsSecret can count as fresh.
        u64 put_seq = 0;
        u64 generation = 0;
    };

    struct Wait {
        RequestId request;
        SecretTarget target;
        u64 raised_after = 0;
        std::optional<SecretBytes> answer;
        UniqueFunction<void(Result<SecretBytes>)> done;
    };

    Impl(ports::ISecretStore& secret_store, WorkerPool& store_pool, WorkerPool& worker_pool, Executor& strand_executor,
         TimerService& timer_service, UserRequestRegistry& request_registry, EventBus& bus, Redactor& masks,
         std::string_view root_hash16, void (*flush)())
        : store(secret_store),
          store_worker(store_pool),
          workers(worker_pool),
          strand(strand_executor),
          timers(timer_service),
          requests(request_registry),
          events(bus),
          redactor(masks),
          flush_logs(flush),
          layout(root_hash16),
          resolutions(bus.subscribe(EventFilter{.kinds = {EventKind::UserActionResolved}}, kResolutionBudget)) {
        resolutions->set_notify([this] { post([this] { drain_resolutions(); }); });
    }

    ~Impl() {
        alive.cancel(CancelReason::Shutdown);
        for (auto& [target, entry] : entries) release(std::move(entry.value));
    }

    template <class F>
    void post(F&& task) {
        strand.post([alive_token = alive.token(), task = std::forward<F>(task)]() mutable {
            if (!alive_token.cancelled()) task();
        });
    }

    template <class T>
    void post_result(UniqueFunction<void(Result<T>)> done, Result<T> result) {
        if (!done) return;
        post([done = std::move(done), result = std::move(result)]() mutable { done(std::move(result)); });
    }

    [[nodiscard]] SecretState state_of(const SecretTarget& target) const {
        const auto it = entries.find(target);
        return it == entries.end() ? SecretState{} : it->second.state;
    }

    void publish_if_changed(const SecretTarget& target, const SecretState& before) {
        const SecretState now = state_of(target);
        if (now == before) return;
        events.publish(EventKind::SecretStateChanged, SecretStateChangedEvent{target, now},
                       EventScope{{}, {}, coalesce_key(target)});
    }

    // The mask is lifted only after the logger has written what was queued while it applied.
    void release(SecretBytes value) {
        workers.submit<void>(
            [&masks = redactor, flush = flush_logs, value = std::move(value)](CancelToken) -> Result<void> {
                flush();
                masks.remove_secret(value.reveal());
                return {};
            },
            CancelToken{}, strand, [](Result<void>) {});
    }

    u64 hold(const SecretTarget& target, SecretBytes value, SecretState state, u64 put_seq) {
        redactor.add_secret(value.reveal());
        auto [it, inserted] = entries.try_emplace(target);
        if (!inserted) release(std::move(it->second.value));
        it->second.value = std::move(value);
        it->second.state = state;
        it->second.put_seq = put_seq;
        it->second.generation = ++last_generation;
        return it->second.generation;
    }

    void drop(const SecretTarget& target) {
        const auto it = entries.find(target);
        if (it == entries.end()) return;
        const SecretState before = it->second.state;
        SecretBytes value = std::move(it->second.value);
        entries.erase(it);
        release(std::move(value));
        publish_if_changed(target, before);
    }

    [[nodiscard]] Diagnostic remember_refused(SecretKind kind) const {
        if (availability.unavailable == SecretsUnavailableReason::NoOsStore)
            return kind_diag(msg::kStoreUnavailable, kind, ErrorKind::Unsupported);
        return make_diag(ErrorDomain::Secrets, msg::kStoreReadFailed);
    }

    [[nodiscard]] std::optional<SecretLocation> remember_location(SecretKind kind) const {
        if (!retention_allowed(kind, Retention::Remember) || phase != Phase::Loaded || !availability.available())
            return std::nullopt;
        return location_for(availability.store);
    }

    [[nodiscard]] bool join_request_pending(const SecretTarget& target) const {
        const std::optional<RequestId> id = join_request_of(target.scope);
        if (!id) return false;
        for (const UserRequest& request : requests.pending())
            if (request.id == *id && request.kind == UserRequestKind::NeedsJoinPassword) return true;
        return false;
    }

    // One store call at a time, so writes land in call order.

    void enqueue(StoreJob job) {
        jobs.push_back(std::move(job));
        pump();
    }

    void pump() {
        while (phase == Phase::Loaded && !busy && !jobs.empty()) {
            StoreJob job = std::move(jobs.front());
            jobs.pop_front();
            if (std::optional<StoreCall> call = job()) start_call(std::move(*call));
        }
    }

    void start_call(StoreCall call) {
        busy = true;
        const u64 id = ++last_call;
        call_done = std::move(call.done);
        if (worker_busy) {
            // An abandoned call still owns the store; a later write must not overtake it.
            post([this, id] { complete_call(id, std::unexpected(timed_out())); });
            return;
        }
        worker_busy = true;
        call_deadline = timers.after(kStoreCallDeadline, [this, id] { complete_call(id, std::unexpected(timed_out())); });
        store_worker.submit<StoreReply>(
            [&target_store = store, work = std::move(call.work)](CancelToken) mutable -> Result<StoreReply> {
                return work(target_store);
            },
            alive.token(), strand,
            [this, id, alive_token = alive.token()](Result<StoreReply> reply) mutable {
                if (alive_token.cancelled()) return;
                worker_busy = false;
                complete_call(id, std::move(reply));
                pump();
            });
    }

    // Whichever of the reply and the deadline comes first wins; the other is discarded.
    void complete_call(u64 id, Result<StoreReply> reply) {
        if (id != last_call || !call_done) return;
        call_deadline.cancel();
        StoreDone done = std::move(call_done);
        call_done = nullptr;
        busy = false;
        done(std::move(reply));
        pump();
    }

    void load() {
        phase = Phase::Loading;
        StoreCall call;
        call.work = [layout_ = layout](ports::ISecretStore& target_store) {
            StoreReply reply;
            reply.kind = target_store.kind();
            if (reply.kind == SecretStoreKind::Unavailable) return reply;
            std::vector<std::vector<u8>> stored_index;
            std::set<SecretTarget> targets;
            for (std::size_t chunk = 0;; ++chunk) {
                Result<std::optional<SecretBytes>> index = target_store.get(layout_.index_key(chunk));
                if (!index) {
                    reply.failure = std::move(index.error());
                    return reply;
                }
                if (!*index) {
                    reply.index_chunks = chunk;
                    break;
                }
                targets.merge(detail::decode_index((*index)->reveal()));
                stored_index.emplace_back((*index)->reveal().begin(), (*index)->reveal().end());
            }
            std::set<SecretTarget> with_value;
            for (const SecretTarget& target : targets) {
                Result<std::optional<SecretBytes>> value = target_store.get(layout_.value_key(target));
                if (!value) {
                    reply.failure = std::move(value.error());
                    return reply;
                }
                // A listed target without a value is dropped from the index on its next write.
                if (!*value) continue;
                with_value.insert(target);
                reply.loaded.emplace_back(target, std::move(**value));
            }
            reply.index_stale = detail::encode_index(with_value) != stored_index;
            return reply;
        };
        call.done = [this](Result<StoreReply> reply) { finish_load(std::move(reply)); };
        start_call(std::move(call));
    }

    void finish_load(Result<StoreReply> reply) {
        if (!succeeded(reply)) {
            const Diagnostic failure = failure_of(reply);
            availability = SecretsAvailability{
                SecretStoreKind::Unavailable, has_error(failure, SecretError::StoreTimedOut)
                                                  ? SecretsUnavailableReason::LoadTimedOut
                                                  : SecretsUnavailableReason::LoadFailed};
            REBOOT_LOG_WARN(Engine, "Saved secrets were not loaded: {}", failure.id);
        } else if (reply->kind == SecretStoreKind::Unavailable) {
            availability = SecretsAvailability{SecretStoreKind::Unavailable, SecretsUnavailableReason::NoOsStore};
        } else {
            availability = SecretsAvailability{reply->kind, std::nullopt};
            index_chunks = reply->index_chunks.value_or(0);
            index_stale = reply->index_stale;
            for (auto& [target, value] : reply->loaded) {
                indexed.insert(target);
                // A put or clear made while loading is newer than the stored copy.
                if (touched_while_loading.contains(target)) continue;
                hold(target, std::move(value), SecretState{location_for(reply->kind), false, false}, 0);
                publish_if_changed(target, SecretState{});
            }
        }
        touched_while_loading.clear();
        phase = Phase::Loaded;
        std::vector<UniqueFunction<void(SecretsAvailability)>> waiters = std::move(start_waiters);
        start_waiters.clear();
        for (auto& waiter : waiters)
            if (waiter) waiter(availability);
    }

    StoreJob write_job(const SecretTarget& target, u64 generation, UniqueFunction<void(Result<SecretState>)> saved) {
        return [this, target, generation, saved = std::move(saved)]() mutable -> std::optional<StoreCall> {
            const auto it = entries.find(target);
            if (it == entries.end() || it->second.generation != generation) {
                // A later put or clear decides what the store keeps.
                post_result(std::move(saved), Result<SecretState>(state_of(target)));
                return std::nullopt;
            }
            if (!availability.available()) {
                const SecretState before = it->second.state;
                it->second.state.write_pending = false;
                it->second.state.not_saved = true;
                publish_if_changed(target, before);
                post_result(std::move(saved), Result<SecretState>(std::unexpected(remember_refused(target.kind))));
                return std::nullopt;
            }
            std::set<SecretTarget> listed = indexed;
            const bool rewrite_index = listed.insert(target).second || index_dirty;
            std::optional<std::vector<std::vector<u8>>> index;
            std::optional<std::vector<std::vector<u8>>> pruned;
            if (rewrite_index) index = detail::encode_index(listed);
            if (rewrite_index && index_stale) pruned = detail::encode_index(indexed);
            const std::size_t chunks = index ? index->size() : 0;

            StoreCall call;
            call.work = [layout_ = layout, value_key = layout.value_key(target), index = std::move(index),
                         pruned = std::move(pruned), stored_chunks = index_chunks,
                         value = copy_of(it->second.value)](ports::ISecretStore& target_store) mutable {
                StoreReply reply;
                // Stale lines go first, so adding the new line only moves lines up.
                std::size_t stored = stored_chunks;
                if (pruned) {
                    write_index(target_store, layout_, *pruned, false, stored, reply);
                    if (reply.failure) return reply;
                    stored = *reply.index_chunks;
                    reply.index_written = false;
                }
                // The index is written first, so it always lists every stored value.
                if (index) {
                    write_index(target_store, layout_, *index, true, stored, reply);
                    if (reply.failure) return reply;
                }
                if (Result<void> put = target_store.put(value_key, value.reveal()); !put) {
                    reply.failure = std::move(put.error());
                    return reply;
                }
                reply.kind = target_store.kind();
                return reply;
            };
            call.done = [this, target, generation, rewrite_index, chunks, listed = std::move(listed),
                         saved = std::move(saved)](Result<StoreReply> reply) mutable {
                note_index_chunks(reply, chunks);
                if (!reply) {
                    // The abandoned call may still store the value, under an index that may not list it.
                    indexed = std::move(listed);
                    if (rewrite_index) index_dirty = true;
                } else if (reply->index_written) {
                    indexed = std::move(listed);
                    index_dirty = false;
                    index_stale = false;
                }
                const bool ok = succeeded(reply);
                if (const auto held = entries.find(target);
                    held != entries.end() && held->second.generation == generation) {
                    const SecretState before = held->second.state;
                    held->second.state.write_pending = false;
                    held->second.state.not_saved = !ok;
                    if (ok)
                        held->second.state.location = location_for(
                            reply->kind == SecretStoreKind::Unavailable ? availability.store : reply->kind);
                    publish_if_changed(target, before);
                }
                if (!saved) return;
                if (ok) saved(state_of(target));
                else saved(std::unexpected(store_failure(msg::kStoreWriteFailed, target.kind, failure_of(reply))));
            };
            return call;
        };
    }

    StoreJob erase_job(const SecretTarget& target, UniqueFunction<void(Result<void>)> done) {
        return [this, target, done = std::move(done)]() mutable -> std::optional<StoreCall> {
            const bool listed = indexed.contains(target);
            // After a failed load the store may still hold a copy nobody could list.
            const bool unknown = !availability.available() &&
                                 availability.unavailable != SecretsUnavailableReason::NoOsStore;
            if (!listed && !unknown) {
                post_result(std::move(done), Result<void>{});
                return std::nullopt;
            }
            std::optional<std::set<SecretTarget>> remaining;
            std::optional<std::vector<std::vector<u8>>> index;
            if (listed) {
                remaining = indexed;
                remaining->erase(target);
                index = detail::encode_index(*remaining);
            }
            const std::size_t chunks = index ? index->size() : 0;

            StoreCall call;
            call.work = [layout_ = layout, value_key = layout.value_key(target), index = std::move(index),
                         stored_chunks = index_chunks](ports::ISecretStore& target_store) {
                StoreReply reply;
                if (Result<void> erased = target_store.erase(value_key); !erased) {
                    reply.failure = std::move(erased.error());
                    return reply;
                }
                // A stale index line only costs a lookup at the next load, so its failure is not reported.
                if (index) {
                    write_index(target_store, layout_, *index, false, stored_chunks, reply);
                    reply.failure.reset();
                }
                return reply;
            };
            call.done = [this, kind = target.kind, chunks, remaining = std::move(remaining),
                         done = std::move(done)](Result<StoreReply> reply) mutable {
                note_index_chunks(reply, chunks);
                if (!reply) {
                    // The abandoned call may still rewrite the index without this target, which stays listed here.
                    if (remaining) index_dirty = true;
                } else if (!reply->failure && remaining) {
                    indexed = std::move(*remaining);
                    if (reply->index_written) index_dirty = index_stale = false;
                }
                if (!done) return;
                if (succeeded(reply)) done(Result<void>{});
                else done(std::unexpected(store_failure(msg::kStoreEraseFailed, kind, failure_of(reply))));
            };
            return call;
        };
    }

    // An abandoned call may have written all of its chunks.
    void note_index_chunks(const Result<StoreReply>& reply, std::size_t chunks) {
        if (!reply) index_chunks = std::max(index_chunks, chunks);
        else if (reply->index_chunks) index_chunks = *reply->index_chunks;
    }

    void require(const SecretTarget& target, SecretWait wait, CancelToken token, UniqueFunction<void(RequestId)> raised,
                 UniqueFunction<void(Result<SecretBytes>)> done) {
        if (wait.reason == NeedsSecretReason::Missing) {
            if (const auto it = entries.find(target); it != entries.end()) {
                post_result(std::move(done), Result<SecretBytes>(copy_of(it->second.value)));
                return;
            }
            // A request raised before the load would ask for a value the store may hold.
            if (phase != Phase::Loaded) {
                start_waiters.push_back([this, target, wait, token = std::move(token), raised = std::move(raised),
                                         done = std::move(done)](SecretsAvailability) mutable {
                    if (token.cancelled())
                        return post_result(std::move(done),
                                           Result<SecretBytes>(std::unexpected(kind_diag(
                                               msg::kRequestWithdrawn, target.kind, ErrorKind::Cancelled))));
                    require(target, wait, std::move(token), std::move(raised), std::move(done));
                });
                if (phase == Phase::Idle) load();
                return;
            }
        }
        const u64 key = ++last_wait;
        NeedsSecret payload{wait.session, target, wait.reason, remember_location(target.kind)};
        const RequestId id = requests.ask(
            UserRequestKind::NeedsSecret, std::move(payload), wait.op, wait.session,
            [this, key, alive_token = alive.token()](const std::any& answer) -> Result<void> {
                if (alive_token.cancelled()) return make_diag(ErrorDomain::Secrets, msg::kNotReady).fail();
                return accept_answer(key, answer);
            },
            std::move(token));
        // A withdrawal during ask() is seen by the posted drain, which runs after this insert.
        waits.emplace(key, Wait{id, target, last_put_seq, std::nullopt, std::move(done)});
        if (raised) raised(id);
    }

    Result<void> accept_answer(u64 key, const std::any& answer) {
        const auto wait = waits.find(key);
        if (wait == waits.end()) return std::unexpected(internal_bug("secret_service"));
        const SecretKind kind = wait->second.target.kind;
        if (std::any_cast<SecretProvided>(&answer) == nullptr)
            return std::unexpected(kind_diag(msg::kAnswerWithoutSecret, kind, ErrorKind::InvalidInput));
        const auto entry = entries.find(wait->second.target);
        if (entry == entries.end() || entry->second.put_seq <= wait->second.raised_after)
            return std::unexpected(kind_diag(msg::kAnswerWithoutSecret, kind, ErrorKind::Conflict));
        wait->second.answer = copy_of(entry->second.value);
        return {};
    }

    void resolved(RequestId id, bool answered) {
        for (auto it = waits.begin(); it != waits.end(); ++it) {
            if (it->second.request != id) continue;
            Wait wait = std::move(it->second);
            waits.erase(it);
            if (!wait.done) break;
            if (answered && wait.answer) wait.done(std::move(*wait.answer));
            else wait.done(std::unexpected(kind_diag(msg::kRequestWithdrawn, wait.target.kind, ErrorKind::Cancelled)));
            break;
        }
        drop(SecretTarget{SecretKind::JoinPassword, SecretScope::join_request(id)});
    }

    void drain_resolutions() {
        std::vector<EventEnvelope> batch;
        while (resolutions->drain(batch, 64) > 0) {
            for (const EventEnvelope& event : batch)
                if (const auto* resolution = std::any_cast<UserActionResolvedEvent>(&event.payload))
                    resolved(resolution->id, resolution->resolution == RequestResolution::Answered);
            batch.clear();
        }
        if (resolutions->take_resync()) reconcile();
    }

    // After a Resync, every request no longer pending counts as resolved.
    void reconcile() {
        std::set<RequestId> pending;
        for (const UserRequest& request : requests.pending()) pending.insert(request.id);
        std::vector<std::pair<RequestId, bool>> gone;
        for (const auto& [key, wait] : waits)
            if (!pending.contains(wait.request)) gone.emplace_back(wait.request, wait.answer.has_value());
        for (const auto& [target, entry] : entries) {
            if (target.kind != SecretKind::JoinPassword) continue;
            if (const auto id = join_request_of(target.scope); id && !pending.contains(*id)) gone.emplace_back(*id, false);
        }
        for (const auto& [id, answered] : gone) resolved(id, answered);
    }

    ports::ISecretStore& store;
    WorkerPool& store_worker;
    WorkerPool& workers;
    Executor& strand;
    TimerService& timers;
    UserRequestRegistry& requests;
    EventBus& events;
    Redactor& redactor;
    void (*flush_logs)();
    detail::StoreLayout layout;
    CancelSource alive;

    Phase phase = Phase::Idle;
    SecretsAvailability availability;
    std::vector<UniqueFunction<void(SecretsAvailability)>> start_waiters;
    std::map<SecretTarget, Entry> entries;
    // Every target that may have a stored value; the store's index lists them all unless index_dirty.
    std::set<SecretTarget> indexed;
    // A call that rewrites the index was abandoned, so the stored index may miss a target.
    bool index_dirty = false;
    // The stored index lists lines `indexed` does not, so the next write that adds one drops them first.
    bool index_stale = false;
    // How many index chunks the store may hold, so a shorter index erases the rest.
    std::size_t index_chunks = 0;
    std::set<SecretTarget> touched_while_loading;
    u64 last_put_seq = 0;
    u64 last_generation = 0;

    std::deque<StoreJob> jobs;
    // A job's store call has not completed.
    bool busy = false;
    // A worker is inside the store, possibly for a call already abandoned at its deadline.
    bool worker_busy = false;
    u64 last_call = 0;
    StoreDone call_done;
    TimerHandle call_deadline;

    std::map<u64, Wait> waits;
    u64 last_wait = 0;
    std::shared_ptr<Subscription> resolutions;
};

SecretService::SecretService(ports::ISecretStore& store, WorkerPool& store_worker, WorkerPool& workers, Executor& strand,
                             TimerService& timers, UserRequestRegistry& requests, EventBus& events, Redactor& redactor,
                             std::string_view root_hash16, void (*flush_logs)())
    : impl_(std::make_unique<Impl>(store, store_worker, workers, strand, timers, requests, events, redactor,
                                   root_hash16, flush_logs)) {}

SecretService::~SecretService() = default;

void SecretService::start(UniqueFunction<void(SecretsAvailability)> done) {
    Impl& s = *impl_;
    if (s.phase == Impl::Phase::Loaded) {
        if (done) s.post([done = std::move(done), availability = s.availability]() mutable { done(availability); });
        return;
    }
    s.start_waiters.push_back(std::move(done));
    if (s.phase == Impl::Phase::Idle) s.load();
}

SecretsAvailability SecretService::availability() const { return impl_->availability; }

Result<void> SecretService::put(const SecretTarget& target, SecretBytes value, std::optional<Retention> retention,
                                UniqueFunction<void(Result<SecretState>)> saved) {
    Impl& s = *impl_;
    const std::size_t size = value.reveal().size();
    if (size == 0) return std::unexpected(kind_diag(msg::kEmptyValue, target.kind, ErrorKind::InvalidInput));
    if (size > kMaxSecretBytes)
        return make_diag(ErrorDomain::Secrets, msg::kTooLarge)
            .kind(ErrorKind::InvalidInput)
            .arg("kind", kind_name(target.kind))
            .arg("max_bytes", kMaxSecretBytes)
            .fail();
    const Retention chosen = retention.value_or(default_retention(target.kind));
    if (!retention_allowed(target.kind, chosen))
        return std::unexpected(kind_diag(msg::kRetentionNotAllowed, target.kind, ErrorKind::InvalidInput));
    if (target.kind == SecretKind::JoinPassword && !s.join_request_pending(target))
        return std::unexpected(scoped_diag(msg::kRequestNotPending, target, ErrorKind::Conflict));

    if (s.phase != Impl::Phase::Loaded) s.touched_while_loading.insert(target);
    const SecretState before = s.state_of(target);
    SecretState state{SecretLocation::Session, false, false};
    // Known refusals are settled now; anything else waits for the store queue, which waits for the load.
    const bool refused = chosen == Retention::Remember && s.phase == Impl::Phase::Loaded && !s.availability.available();
    if (chosen == Retention::Remember) {
        state.write_pending = !refused;
        state.not_saved = refused;
    }
    const u64 generation = s.hold(target, std::move(value), state, ++s.last_put_seq);
    s.publish_if_changed(target, before);

    if (refused) {
        s.post_result(std::move(saved), Result<SecretState>(std::unexpected(s.remember_refused(target.kind))));
    } else if (chosen == Retention::Remember) {
        s.enqueue(s.write_job(target, generation, std::move(saved)));
    } else if (detail::storable(target.kind)) {
        s.enqueue(s.erase_job(target, [&s, target, saved = std::move(saved)](Result<void> erased) mutable {
            if (!saved) return;
            if (erased) saved(s.state_of(target));
            else saved(std::unexpected(std::move(erased.error())));
        }));
    } else if (saved) {
        s.post([&s, target, saved = std::move(saved)]() mutable { saved(s.state_of(target)); });
    }
    return {};
}

Result<SecretState> SecretService::state(const SecretTarget& target) const {
    if (impl_->phase != Impl::Phase::Loaded) return std::unexpected(not_ready());
    return impl_->state_of(target);
}

void SecretService::clear(const SecretTarget& target, UniqueFunction<void(Result<void>)> done) {
    Impl& s = *impl_;
    if (s.phase != Impl::Phase::Loaded) s.touched_while_loading.insert(target);
    s.drop(target);
    if (detail::storable(target.kind)) s.enqueue(s.erase_job(target, std::move(done)));
    else s.post_result(std::move(done), Result<void>{});
}

Result<SecretBytes> SecretService::provide(const SecretTarget& target) const {
    const auto it = impl_->entries.find(target);
    if (it != impl_->entries.end()) return copy_of(it->second.value);
    if (impl_->phase != Impl::Phase::Loaded) return std::unexpected(not_ready());
    return std::unexpected(scoped_diag(msg::kNotFound, target, ErrorKind::NotFound));
}

Result<SecretBytes> SecretService::take(const SecretTarget& target) {
    Impl& s = *impl_;
    if (target.kind != SecretKind::JoinPassword)
        return std::unexpected(kind_diag(msg::kRevealForbidden, target.kind, ErrorKind::Unsupported));
    const auto it = s.entries.find(target);
    if (it == s.entries.end()) return std::unexpected(scoped_diag(msg::kNotFound, target, ErrorKind::NotFound));
    SecretBytes value = std::move(it->second.value);
    // drop() releases this copy, so the mask outlives the records logged before the handover.
    it->second.value = copy_of(value);
    s.drop(target);
    return value;
}

Result<SecretBytes> SecretService::reveal(const SecretTarget& target) const {
    if (!revealable(target.kind))
        return std::unexpected(kind_diag(msg::kRevealForbidden, target.kind, ErrorKind::Unsupported));
    return provide(target);
}

void SecretService::require(const SecretTarget& target, SecretWait wait, CancelToken token,
                            UniqueFunction<void(RequestId)> raised, UniqueFunction<void(Result<SecretBytes>)> done) {
    impl_->require(target, wait, std::move(token), std::move(raised), std::move(done));
}

}  // namespace rb::secrets
