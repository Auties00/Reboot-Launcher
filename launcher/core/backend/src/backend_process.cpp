#include "reboot/backend/backend_process.hpp"

#include <algorithm>
#include <format>
#include <map>
#include <span>
#include <string_view>
#include <utility>

#include "reboot/backend/backend_process_observer.hpp"
#include "reboot/backend/match_target_resolver.hpp"
#include "reboot/foundation/executor.hpp"
#include "reboot/process/child_observer.hpp"
#include "reboot/process/child_request_handler.hpp"
#include "wire_values.hpp"

namespace reboot::backend {

namespace {

namespace be = contracts::backend;

constexpr std::string_view kChildGoneId = "process.child_gone";

void log(LogLevel level, std::string text) {
    if (Logger::enabled(level)) Logger::write(level, LogCategory::Backend, std::nullopt, std::move(text));
}

[[nodiscard]] std::span<const u8> bytes_of(const std::string& text) noexcept {
    return {reinterpret_cast<const u8*>(text.data()), text.size()};
}

[[nodiscard]] be::ConfigureSession configure_message(const BackendSessionConfig& config) {
    return be::ConfigureSession{.req_id = 0,
                                .session_key = config.session_key.reveal(),
                                .account_id = config.account_id,
                                .origin = config.origin.reveal(),
                                .console_key = config.console_key.name,
                                .build = be::GameBuild{config.version.canonical(), config.changelist.value}};
}

[[nodiscard]] std::vector<BackendAccount> to_accounts(const std::vector<be::AccountSummary>& summaries) {
    std::vector<BackendAccount> accounts;
    accounts.reserve(summaries.size());
    for (const be::AccountSummary& summary : summaries) accounts.push_back(to_account(summary));
    return accounts;
}

}  // namespace

struct BackendProcess::Impl final : process::ChildObserver, process::ChildRequestHandler {
    struct Rename {
        std::string old_account_id;
        std::string new_account_id;
        be::RenameConflictPolicy on_conflict{};
        UniqueFunction<void(Result<void>)> done;
        bool sent = false;
    };

    Impl(ports::IProcessLauncher& launcher, Executor& strand_ref, TimerService& timers, const IClock& clock,
         Redactor& redactor_ref, process::ProcessSpec spec, process::ChildRecordCallback record, LogLevel level)
        : strand(strand_ref),
          redactor(redactor_ref),
          log_level(level),
          data_root(to_wire(spec.working_directory())),
          supervisor(launcher, strand_ref, timers, clock, std::move(spec), handshake(), options(), *this, *this,
                     std::move(record)) {}

    [[nodiscard]] process::ChildHandshake handshake() {
        return process::make_child_handshake<be::BackendHello>(
            be::kBackendProtocol, [](const be::BackendHello& hello) { return hello.protocol; },
            [this](const be::BackendHello& hello) -> Result<be::BackendWelcome> {
                hello_build = hello.build;
                hello_content = hello.content;
                return be::BackendWelcome{std::string(config.bind_address()), data_root, log_level};
            });
    }

    [[nodiscard]] static process::ChildSupervisorOptions options() {
        process::ChildSupervisorOptions options;
        options.log_category = LogCategory::Backend;
        options.restart = process::RestartPolicy{};
        return options;
    }

    // Ready arrived for the live child, so requests reach a listening backend.
    [[nodiscard]] bool live() const noexcept {
        return supervisor.state() == process::ChildState::Running && ready_generation != 0 &&
               ready_generation == supervisor.generation();
    }

    // The supervisor fails a dead child's requests before on_exit, so liveness is checked here too.
    [[nodiscard]] bool current(u32 generation) const noexcept { return live() && ready_generation == generation; }

    // An answer that must still arrive on the strand, but only while this object exists.
    void post(UniqueFunction<void()> task) {
        strand.post([alive = alive.token(), task = std::move(task)]() mutable {
            if (!alive.cancelled()) task();
        });
    }

    void succeed_later(UniqueFunction<void(Result<void>)> done) {
        post([done = std::move(done)]() mutable {
            if (done) done({});
        });
    }

    // A rename to this id is pending, so registering it now would create the account it moves to.
    [[nodiscard]] bool held(const std::string& account_id) const {
        return std::ranges::any_of(renames, [&](const auto& rename) { return rename->new_account_id == account_id; });
    }

    [[nodiscard]] bool used_by_session(const std::string& account_id) const {
        return std::ranges::any_of(sessions, [&](const auto& entry) { return entry.second.account_id == account_id; });
    }

    // `replay` names the generation whose replay this send belongs to.
    void send_register(const AccountRegistration& account, std::optional<u32> replay) {
        supervisor.command(be::RegisterAccount{0, account.account_id, account.record.value, account.role},
                           [this, id = account.account_id, replay](Result<void> result) {
                               if (!result)
                                   log(LogLevel::Warn, std::format("RegisterAccount {} failed: {}", id, result.error().id));
                               if (replay) replayed(*replay);
                           });
    }

    void send_configure(const BackendSessionConfig& session, std::optional<u32> replay,
                        UniqueFunction<void(Result<void>)> done) {
        supervisor.command(configure_message(session),
                           [this, replay, done = std::move(done)](Result<void> result) mutable {
                               if (done) done(std::move(result));
                               else if (!result)
                                   log(LogLevel::Warn, std::format("ConfigureSession replay failed: {}", result.error().id));
                               if (replay) replayed(*replay);
                           });
    }

    void send_rename(Rename& rename, std::optional<u32> replay) {
        rename.sent = true;
        supervisor.command(be::RenameAccount{0, rename.old_account_id, rename.new_account_id, rename.on_conflict},
                           [this, target = &rename, replay](Result<void> result) {
                               if (!result && result.error().id == kChildGoneId) retry_rename(*target);
                               else finish_rename(*target, std::move(result));
                               if (replay) replayed(*replay);
                           });
    }

    // The child died before answering, so the rename goes again to the next generation.
    void retry_rename(Rename& rename) {
        const auto found = std::ranges::find_if(renames, [&](const auto& entry) { return entry.get() == &rename; });
        if (found != renames.end()) (*found)->sent = false;
    }

    void finish_rename(Rename& rename, Result<void> result) {
        const auto found = std::ranges::find_if(renames, [&](const auto& entry) { return entry.get() == &rename; });
        if (found == renames.end()) return;
        std::unique_ptr<Rename> finished = std::move(*found);
        renames.erase(found);
        if (live() && !held(finished->new_account_id))
            for (const AccountRegistration& account : accounts)
                if (account.account_id == finished->new_account_id) send_register(account, std::nullopt);
        if (finished->done) finished->done(std::move(result));
    }

    // Sends every rename no live session blocks.
    void send_renames(std::optional<u32> replay) {
        std::vector<Rename*> ready;
        for (const auto& rename : renames)
            if (!rename->sent && !used_by_session(rename->old_account_id)) ready.push_back(rename.get());
        for (Rename* rename : ready) {
            if (replay) ++replay_outstanding;
            send_rename(*rename, replay);
        }
    }

    // Renames first, so a registration never creates the account a rename moves to.
    void replay(u32 generation) {
        replay_outstanding = 1;
        send_renames(generation);
        for (const AccountRegistration& account : accounts) {
            if (held(account.account_id)) continue;
            ++replay_outstanding;
            send_register(account, generation);
        }
        for (const auto& entry : sessions) {
            ++replay_outstanding;
            send_configure(entry.second, generation, nullptr);
        }
        replayed(generation);
    }

    void replayed(u32 generation) {
        if (!current(generation) || replay_outstanding == 0) return;
        if (--replay_outstanding != 0) return;
        if (observer != nullptr) observer->on_ready(BackendReady{generation, http_port, hello_build, hello_content});
    }

    // process::ChildObserver
    void on_running(u32) override {
        ready_generation = 0;
        replay_outstanding = 0;
    }

    void on_event(const RawFrame& frame) override {
        if (is_frame<be::Ready>(frame)) return on_ready_frame(frame);
        if (is_frame<be::LoginObserved>(frame)) return on_login(frame);
        if (is_frame<be::AccountRenameConflict>(frame)) {
            Result<be::AccountRenameConflict> conflict = decode_contract<be::AccountRenameConflict>(frame.payload);
            if (!conflict) return log(LogLevel::Warn, "Undecodable AccountRenameConflict from the backend");
            if (rename_conflicts) rename_conflicts(*conflict);
            return;
        }
        log(LogLevel::Debug, std::format("Ignored backend frame type {:#x}", frame.type));
    }

    void on_ready_frame(const RawFrame& frame) {
        const u32 generation = supervisor.generation();
        if (ready_generation == generation) return;
        Result<be::Ready> ready = decode_contract<be::Ready>(frame.payload);
        if (!ready) return log(LogLevel::Warn, "Undecodable Ready from the backend");
        ready_generation = generation;
        http_port = ready->http_port;
        replay(generation);
    }

    void on_login(const RawFrame& frame) {
        Result<be::LoginObserved> login = decode_contract<be::LoginObserved>(frame.payload);
        if (!login) return log(LogLevel::Warn, "Undecodable LoginObserved from the backend");
        const auto found = std::ranges::find_if(
            sessions, [&](const auto& entry) { return entry.second.session_key.reveal() == login->session_key; });
        if (found == sessions.end() || observer == nullptr) return;
        observer->on_login_observed(LoginObservedEvent{found->first, std::move(login->account_id)});
    }

    void on_unresponsive(u32 missed) override {
        log(LogLevel::Warn, std::format("The backend missed {} pings in a row", missed));
    }

    void on_exit(const process::ChildExitInfo& exit) override {
        ready_generation = 0;
        replay_outstanding = 0;
        if (observer != nullptr) observer->on_exit(exit);
    }

    // process::ChildRequestHandler
    [[nodiscard]] bool handles(u64 frame_type) const override {
        return frame_type == contract_frame_type_v<be::ResolveMatchTarget>;
    }

    void on_request(const RawFrame& frame, process::ChildReply reply) override {
        Result<be::ResolveMatchTarget> request = decode_contract<be::ResolveMatchTarget>(frame.payload);
        if (!request) return reply.fail(request.error());
        be::MatchTarget answer;
        if (resolver != nullptr) {
            const ResolvedMatchTarget target = resolver->resolve(MatchTargetQuery{request->account_id, request->playlist});
            if (target.endpoint) {
                answer.endpoint = host_port_text(*target.endpoint);
                if (target.beacon_port) answer.beacon_port = target.beacon_port->value;
            }
        }
        reply.reply(std::move(answer));
    }

    Executor& strand;
    Redactor& redactor;
    LogLevel log_level;
    WirePath data_root;
    BackendConfig config;
    std::string hello_build;
    be::ContentVersion hello_content;
    u32 ready_generation = 0;
    u16 http_port = 0;
    std::size_t replay_outstanding = 0;

    IBackendProcessObserver* observer = nullptr;
    IMatchTargetResolver* resolver = nullptr;
    UniqueFunction<void(const be::AccountRenameConflict&)> rename_conflicts;

    std::vector<AccountRegistration> accounts;
    std::map<SessionId, BackendSessionConfig> sessions;
    std::vector<std::unique_ptr<Rename>> renames;
    CancelSource alive;

    // Last, so it is destroyed first: it kills the child and drops callbacks that reach the state above.
    process::ChildSupervisor supervisor;
};

BackendProcess::BackendProcess(ports::IProcessLauncher& launcher, Executor& strand, TimerService& timers,
                               const IClock& clock, Redactor& redactor, process::ProcessSpec spec,
                               process::ChildRecordCallback record, LogLevel log_level)
    : impl_(std::make_unique<Impl>(launcher, strand, timers, clock, redactor, std::move(spec), std::move(record),
                                   log_level)) {}

BackendProcess::~BackendProcess() { impl_->alive.cancel(CancelReason::Shutdown); }

void BackendProcess::set_observer(IBackendProcessObserver* observer) { impl_->observer = observer; }

void BackendProcess::set_match_target_resolver(IMatchTargetResolver* resolver) { impl_->resolver = resolver; }

void BackendProcess::set_rename_conflict_handler(
    UniqueFunction<void(const contracts::backend::AccountRenameConflict&)> handler) {
    impl_->rename_conflicts = std::move(handler);
}

Result<void> BackendProcess::start(const BackendConfig& config) {
    impl_->config = config;
    return impl_->supervisor.start();
}

bool BackendProcess::stop(std::chrono::milliseconds grace) { return impl_->supervisor.stop(grace); }

process::ChildState BackendProcess::state() const noexcept { return impl_->supervisor.state(); }

u32 BackendProcess::generation() const noexcept { return impl_->supervisor.generation(); }

std::optional<u32> BackendProcess::pid() const noexcept { return impl_->supervisor.pid(); }

void BackendProcess::register_account(AccountRegistration account) {
    Impl& impl = *impl_;
    const auto found = std::ranges::find(impl.accounts, account.record, &AccountRegistration::record);
    if (found != impl.accounts.end()) *found = account;
    else impl.accounts.push_back(account);
    if (impl.live() && !impl.held(account.account_id)) impl.send_register(account, std::nullopt);
}

void BackendProcess::configure_session(SessionId session, BackendSessionConfig config,
                                       UniqueFunction<void(Result<void>)> done) {
    Impl& impl = *impl_;
    const auto [entry, inserted] = impl.sessions.insert_or_assign(session, std::move(config));
    if (!impl.live()) return impl.succeed_later(std::move(done));
    impl.send_configure(entry->second, std::nullopt, std::move(done));
}

void BackendProcess::end_session(SessionId session, UniqueFunction<void(Result<void>)> done) {
    Impl& impl = *impl_;
    const auto found = impl.sessions.find(session);
    if (found == impl.sessions.end()) return impl.succeed_later(std::move(done));
    std::string key = found->second.session_key.reveal();
    impl.sessions.erase(found);
    if (impl.live()) {
        impl.supervisor.command(be::EndSession{0, std::move(key)}, [done = std::move(done)](Result<void> ended) mutable {
            if (done) done(std::move(ended));
        });
        impl.send_renames(std::nullopt);
    } else {
        impl.succeed_later(std::move(done));
    }
}

void BackendProcess::rename_account(std::string old_account_id, std::string new_account_id,
                                    contracts::backend::RenameConflictPolicy on_conflict,
                                    UniqueFunction<void(Result<void>)> done) {
    Impl& impl = *impl_;
    impl.renames.push_back(std::make_unique<Impl::Rename>(
        Impl::Rename{std::move(old_account_id), std::move(new_account_id), on_conflict, std::move(done)}));
    if (impl.live()) impl.send_renames(std::nullopt);
}

void BackendProcess::health(UniqueFunction<void(Result<BackendHealth>)> done) {
    impl_->supervisor.request<be::HealthReply>(be::Health{}, [done = std::move(done)](Result<be::HealthReply> reply) mutable {
        if (!reply) return done(std::unexpected(std::move(reply.error())));
        done(BackendHealth{std::move(reply->version), reply->content, reply->http_port});
    });
}

void BackendProcess::mint_launch_credential(SessionId session, LaunchCredentialRequest request,
                                            UniqueFunction<void(Result<LaunchCredential>)> done) {
    Impl& impl = *impl_;
    const auto configured = impl.sessions.find(session);
    if (configured == impl.sessions.end())
        return impl.post([done = std::move(done)]() mutable {
            done(std::unexpected(internal_bug("backend.mint_for_unconfigured_session")));
        });
    if (configured->second.account_id != request.account_id)
        return impl.post([done = std::move(done)]() mutable {
            done(std::unexpected(internal_bug("backend.mint_for_other_account")));
        });
    impl.supervisor.request<be::LaunchCredential>(
        be::MintLaunchCredential{0, std::move(request.account_id), request.kind, request.build.value},
        [&redactor = impl.redactor, done = std::move(done)](Result<be::LaunchCredential> reply) mutable {
            if (!reply) return done(std::unexpected(std::move(reply.error())));
            SecretString value(std::move(reply->value));
            redactor.add_secret(bytes_of(value.reveal()));
            done(LaunchCredential{std::move(value), from_unix_ms(reply->expires_at_unix_ms)});
        });
}

void BackendProcess::list_accounts(UniqueFunction<void(Result<std::vector<BackendAccount>>)> done) {
    impl_->supervisor.request<be::AccountsReply>(
        be::AccountsList{}, [done = std::move(done)](Result<be::AccountsReply> reply) mutable {
            if (!reply) return done(std::unexpected(std::move(reply.error())));
            done(to_accounts(reply->accounts));
        });
}

void BackendProcess::reset_account(std::string account_id, UniqueFunction<void(Result<void>)> done) {
    impl_->supervisor.command(be::AccountsReset{0, std::move(account_id)}, std::move(done));
}

void BackendProcess::delete_account(std::string account_id, UniqueFunction<void(Result<void>)> done) {
    impl_->supervisor.command(be::AccountsDelete{0, std::move(account_id)}, std::move(done));
}

void BackendProcess::prune_accounts(AccountPruneFilter filter,
                                    UniqueFunction<void(Result<std::vector<BackendAccount>>)> done) {
    impl_->supervisor.request<be::AccountsReply>(
        be::AccountsPrune{0, to_unix_ms(filter.last_login_before), filter.role},
        [done = std::move(done)](Result<be::AccountsReply> reply) mutable {
            if (!reply) return done(std::unexpected(std::move(reply.error())));
            done(to_accounts(reply->accounts));
        });
}

void BackendProcess::purge_data(UniqueFunction<void(Result<void>)> done) {
    impl_->supervisor.command(be::PurgeData{}, std::move(done));
}

void BackendProcess::drain(UniqueFunction<void(Result<void>)> done) {
    impl_->supervisor.command(be::Drain{}, std::move(done));
}

}  // namespace reboot::backend
