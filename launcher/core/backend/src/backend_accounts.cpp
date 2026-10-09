#include "reboot/backend/backend_accounts.hpp"

#include <any>
#include <format>
#include <map>
#include <type_traits>
#include <utility>

#include "messages.hpp"
#include "reboot/backend/account_registration.hpp"
#include "reboot/backend/account_rename_conflict.hpp"
#include "reboot/backend/backend_accounts_changed.hpp"
#include "reboot/backend/backend_process.hpp"
#include "reboot/backend/backend_service.hpp"
#include "reboot/foundation/events.hpp"
#include "reboot/foundation/log.hpp"
#include "reboot/foundation/user_request.hpp"
#include "reboot/identity/account_record.hpp"

namespace reboot::backend {

namespace {

namespace be = contracts::backend;

void log(LogLevel level, std::string text) {
    if (Logger::enabled(level)) Logger::write(level, LogCategory::Backend, std::nullopt, std::move(text));
}

[[nodiscard]] be::RenameConflictPolicy policy_for(RenameConflictChoice choice) noexcept {
    switch (choice) {
        case RenameConflictChoice::KeepExisting: return be::RenameConflictPolicy::KeepTarget;
        case RenameConflictChoice::Replace: return be::RenameConflictPolicy::Overwrite;
        case RenameConflictChoice::Ask: break;
    }
    return be::RenameConflictPolicy::Report;
}

[[nodiscard]] AccountRegistration registration_of(const identity::AccountRecord& record) {
    return AccountRegistration{identity::account_id(record), record.record_id, record.role};
}

template <class T>
[[nodiscard]] Outcome<T> outcome_of(Result<T> result) {
    if (!result) return Failed{std::move(result.error())};
    if constexpr (std::is_void_v<T>) return Completed<void>{};
    else return Completed<T>{std::move(*result)};
}

}  // namespace

struct BackendAccounts::Impl {
    using RenameKey = std::pair<std::string, std::string>;

    // One account op: its maintenance lease and the cancellation that ends it early.
    struct Task {
        BackendLease lease;
        CancelRegistration cancel;
        // Completes the op with Cancelled; for the destructor.
        UniqueFunction<void(CancelReason)> abandon;
        bool finished = false;
    };

    Impl(BackendService& service_ref, BackendProcess& process_ref, OpRegistry& ops_ref,
         UserRequestRegistry& requests_ref, EventBus& events_ref)
        : service(service_ref), process(process_ref), ops(ops_ref), requests(requests_ref), events(events_ref) {}

    // BackendProcess outlives this object, so its answers are dropped once this is gone.
    template <class F>
    [[nodiscard]] auto guarded(F callback) {
        return [alive = alive.token(), callback = std::move(callback)]<class... A>(A&&... args) mutable {
            if (!alive.cancelled()) callback(std::forward<A>(args)...);
        };
    }

    // --- the list

    void reload(UniqueFunction<void()> then) {
        process.list_accounts([this, alive = alive.token(), then = std::move(then)](
                                  Result<std::vector<BackendAccount>> listed) mutable {
            if (alive.cancelled()) return;
            if (listed) {
                accounts = std::move(*listed);
                events.publish(EventKind::BackendAccountsChanged, BackendAccountsChanged{*accounts});
            } else {
                log(LogLevel::Warn, std::format("Listing the backend accounts failed: {}", listed.error().id));
            }
            if (then) then();
        });
    }

    // --- tasks

    // Finished tasks are erased here rather than where they finish, since a cancellation callback
    // must not destroy its own registration.
    void reap() {
        std::erase_if(tasks, [](const auto& entry) { return entry.second->finished; });
    }

    [[nodiscard]] Task* live_task(u64 id) {
        const auto found = tasks.find(id);
        return found == tasks.end() || found->second->finished ? nullptr : found->second.get();
    }

    template <class T>
    void finish(u64 id, Operation<T>& op, Result<T> result) {
        Task* task = live_task(id);
        if (task == nullptr) return;
        task->finished = true;
        op.complete(outcome_of(std::move(result)));
        task->lease.release();
    }

    // Takes the lease, waits for the backend, then runs `body`; `body` ends the op through finish().
    template <class T>
    Result<OpHandle> start(DisconnectPolicy policy, UniqueFunction<void(u64, Operation<T>&)> body) {
        reap();
        Result<BackendLease> lease = service.acquire_maintenance();
        if (!lease) return std::unexpected(std::move(lease.error()));
        auto [handle, op] = ops.create<T>(OpKind::Generic, policy, std::nullopt);
        const u64 id = next_task++;
        Task& task = *tasks.emplace(id, std::make_unique<Task>()).first->second;
        task.lease = std::move(*lease);
        Operation<T>* operation = &op;
        task.abandon = [operation](CancelReason reason) { operation->complete(Cancelled{reason}); };
        task.cancel = op.token().on_cancel([this, id, operation](CancelReason reason) {
            Task* cancelled = live_task(id);
            if (cancelled == nullptr) return;
            cancelled->finished = true;
            operation->complete(Cancelled{reason});
            cancelled->lease.release();
        });
        service.ensure_ready(task.lease, op.token(),
                             [this, alive = alive.token(), id, operation,
                              body = std::move(body)](Result<BackendUpstream> ready) mutable {
                                 if (alive.cancelled() || live_task(id) == nullptr) return;
                                 if (!ready) return finish<T>(id, *operation, std::unexpected(std::move(ready.error())));
                                 body(id, *operation);
                             });
        return handle;
    }

    // Reloads the list, then completes the op with `result` whatever the reload found.
    template <class T>
    void finish_after_reload(u64 id, Operation<T>& op, Result<T> result) {
        if (live_task(id) == nullptr) return;
        reload([this, id, &op, result = std::move(result)]() mutable { finish<T>(id, op, std::move(result)); });
    }

    [[nodiscard]] static Diagnostic in_use(u32 sessions) {
        return make_diag(ErrorDomain::Backend, msg::kInUse).arg("sessions", sessions).kind(ErrorKind::Conflict).build();
    }

    // --- renames

    [[nodiscard]] std::optional<be::AccountRenameConflict> take_conflict(const RenameKey& key) {
        const auto found = conflicts.find(key);
        if (found == conflicts.end()) return std::nullopt;
        be::AccountRenameConflict conflict = std::move(found->second);
        conflicts.erase(found);
        return conflict;
    }

    // `op` absent: an identity rename, asked about with no op waiting.
    RequestId ask_conflict(const be::AccountRenameConflict& conflict, std::optional<OpId> op, CancelToken token,
                           UniqueFunction<void(RenameConflictChoice)> chosen) {
        AccountRenameConflictPrompt prompt{conflict.old_account_id, conflict.new_account_id, std::nullopt};
        if (conflict.existing_record_id) prompt.existing_record = AccountRecordId{*conflict.existing_record_id};
        return requests.ask(
            UserRequestKind::AccountRenameConflict, std::move(prompt), op, std::nullopt,
            [alive = alive.token(), chosen = std::move(chosen)](const std::any& answer) mutable -> Result<void> {
                const auto* decision = std::any_cast<AccountRenameConflictAnswer>(&answer);
                if (decision == nullptr || decision->choice == RenameConflictChoice::Ask)
                    return make_diag(ErrorDomain::Backend, msg::kAnswerInvalid).kind(ErrorKind::InvalidInput).fail();
                if (!alive.cancelled()) chosen(decision->choice);
                return {};
            },
            std::move(token));
    }

    void rename_identity(std::string old_id, std::string new_id) {
        process.rename_account(
            old_id, new_id, be::RenameConflictPolicy::Report,
            [this, alive = alive.token(), old_id, new_id](Result<void> renamed) {
                if (alive.cancelled()) return;
                if (renamed) return reload(nullptr);
                const std::optional<be::AccountRenameConflict> conflict = take_conflict({old_id, new_id});
                if (!conflict) {
                    log(LogLevel::Warn, std::format("Renaming backend account {} to {} failed: {}", old_id, new_id,
                                                    renamed.error().id));
                    return;
                }
                ask_conflict(*conflict, std::nullopt, this->alive.token(), [this, old_id, new_id](RenameConflictChoice choice) {
                    process.rename_account(old_id, new_id, policy_for(choice),
                                           [this, alive = this->alive.token(), old_id, new_id](Result<void> resolved) {
                                               if (alive.cancelled()) return;
                                               if (!resolved)
                                                   log(LogLevel::Warn,
                                                       std::format("Renaming backend account {} to {} failed: {}",
                                                                   old_id, new_id, resolved.error().id));
                                               reload(nullptr);
                                           });
                });
            });
    }

    void rename_for_op(u64 id, Operation<std::string>& op, AccountRenameRequest rename, be::RenameConflictPolicy policy) {
        process.rename_account(
            rename.old_account_id, rename.new_account_id, policy,
            guarded([this, id, &op, rename](Result<void> renamed) mutable {
                const RenameKey key{rename.old_account_id, rename.new_account_id};
                const std::optional<be::AccountRenameConflict> conflict = take_conflict(key);
                if (live_task(id) == nullptr) return;
                if (renamed) return finish_after_reload<std::string>(id, op, std::move(rename.new_account_id));
                if (!conflict || rename.on_conflict != RenameConflictChoice::Ask)
                    return finish<std::string>(id, op, std::unexpected(std::move(renamed.error())));
                const RequestId asked = ask_conflict(*conflict, op.id(), op.token(),
                                                     [this, id, &op, rename](RenameConflictChoice choice) mutable {
                                                         if (live_task(id) == nullptr) return;
                                                         rename_for_op(id, op, std::move(rename), policy_for(choice));
                                                     });
                op.awaiting_user(asked);
            }));
    }

    BackendService& service;
    BackendProcess& process;
    OpRegistry& ops;
    UserRequestRegistry& requests;
    EventBus& events;

    std::optional<std::vector<BackendAccount>> accounts;
    // The account id each identity record was last registered under.
    std::map<AccountRecordId, AccountRegistration> registered;
    // AccountRenameConflict events, read by the rename whose reply follows them.
    std::map<RenameKey, be::AccountRenameConflict> conflicts;
    std::map<u64, std::unique_ptr<Task>> tasks;
    u64 next_task = 1;
    CancelSource alive;
};

BackendAccounts::BackendAccounts(BackendService& service, BackendProcess& process, OpRegistry& ops,
                                 UserRequestRegistry& requests, EventBus& events)
    : impl_(std::make_unique<Impl>(service, process, ops, requests, events)) {
    Impl& impl = *impl_;
    service.add_ready_listener([&impl, alive = impl.alive.token()](const BackendState& state) {
        if (!alive.cancelled() && state.config.target.embedded()) impl.reload(nullptr);
    });
    process.set_rename_conflict_handler([&impl](const contracts::backend::AccountRenameConflict& conflict) {
        impl.conflicts.insert_or_assign({conflict.old_account_id, conflict.new_account_id}, conflict);
    });
}

BackendAccounts::~BackendAccounts() {
    Impl& impl = *impl_;
    impl.alive.cancel(CancelReason::Shutdown);
    impl.process.set_rename_conflict_handler(nullptr);
    for (auto& [id, task] : impl.tasks) {
        if (task->finished) continue;
        task->finished = true;
        task->cancel.reset();
        task->abandon(CancelReason::Shutdown);
        task->lease.release();
    }
}

void BackendAccounts::register_identity(const identity::IdentitySnapshot& identity) {
    for (const identity::AccountRecord* record : {&identity.client, &identity.host}) {
        AccountRegistration registration = registration_of(*record);
        impl_->registered.insert_or_assign(registration.record, registration);
        impl_->process.register_account(std::move(registration));
    }
}

void BackendAccounts::on_identity_changed(const identity::IdentityChangedEvent& event) {
    Impl& impl = *impl_;
    AccountRegistration registration = registration_of(event.record);
    const auto known = impl.registered.find(registration.record);
    if (known != impl.registered.end() && known->second.account_id != registration.account_id)
        impl.rename_identity(known->second.account_id, registration.account_id);
    impl.registered.insert_or_assign(registration.record, registration);
    impl.process.register_account(std::move(registration));
}

Result<std::vector<BackendAccount>> BackendAccounts::list() const {
    if (!impl_->accounts) return make_diag(ErrorDomain::Backend, msg::kAccountsNotLoaded).retryable().fail();
    return *impl_->accounts;
}

Result<OpHandle> BackendAccounts::start_reset(std::string account_id, DisconnectPolicy policy) {
    Impl& impl = *impl_;
    return impl.start<void>(policy, [&impl, account_id = std::move(account_id)](u64 id, Operation<void>& op) mutable {
        impl.process.reset_account(std::move(account_id), impl.guarded([&impl, id, &op](Result<void> reset) {
            impl.finish_after_reload<void>(id, op, std::move(reset));
        }));
    });
}

Result<OpHandle> BackendAccounts::start_delete(std::string account_id, DisconnectPolicy policy) {
    Impl& impl = *impl_;
    return impl.start<void>(policy, [&impl, account_id = std::move(account_id)](u64 id, Operation<void>& op) mutable {
        impl.process.delete_account(std::move(account_id), impl.guarded([&impl, id, &op](Result<void> deleted) {
            impl.finish_after_reload<void>(id, op, std::move(deleted));
        }));
    });
}

Result<OpHandle> BackendAccounts::start_prune(AccountPruneFilter filter, DisconnectPolicy policy) {
    Impl& impl = *impl_;
    return impl.start<u64>(policy, [&impl, filter](u64 id, Operation<u64>& op) {
        impl.process.prune_accounts(filter, impl.guarded([&impl, id, &op, filter](Result<std::vector<BackendAccount>> removed) {
            if (!removed) return impl.finish<u64>(id, op, std::unexpected(std::move(removed.error())));
            for (const BackendAccount& account : *removed)
                if (!filter.selects(account))
                    return impl.finish_after_reload<u64>(
                        id, op, std::unexpected(internal_bug("backend.prune_removed_unselected_account")));
            impl.finish_after_reload<u64>(id, op, static_cast<u64>(removed->size()));
        }));
    });
}

Result<OpHandle> BackendAccounts::start_rename(AccountRenameRequest rename, DisconnectPolicy policy) {
    Impl& impl = *impl_;
    return impl.start<std::string>(policy, [&impl, rename = std::move(rename)](u64 id, Operation<std::string>& op) mutable {
        const contracts::backend::RenameConflictPolicy first = policy_for(rename.on_conflict);
        impl.rename_for_op(id, op, std::move(rename), first);
    });
}

Result<OpHandle> BackendAccounts::start_purge(DisconnectPolicy policy) {
    Impl& impl = *impl_;
    if (const u32 live = impl.service.session_leases(); live != 0) return std::unexpected(impl.in_use(live));
    return impl.start<void>(policy, [&impl](u64 id, Operation<void>& op) {
        // A session may have taken a lease while the backend was starting.
        if (const u32 live = impl.service.session_leases(); live != 0)
            return impl.finish<void>(id, op, std::unexpected(impl.in_use(live)));
        impl.process.purge_data(impl.guarded([&impl, id, &op](Result<void> purged) {
            // Purging drops the registrations too; the identity records still need theirs.
            if (purged)
                for (const auto& entry : impl.registered) impl.process.register_account(entry.second);
            impl.finish_after_reload<void>(id, op, std::move(purged));
        }));
    });
}

}  // namespace reboot::backend
