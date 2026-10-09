#include "reboot/browser/join_service.hpp"

#include <any>
#include <cstring>
#include <map>
#include <utility>

#include "messages.hpp"
#include "reboot/browser/browser_session.hpp"
#include "reboot/browser/join_prompts.hpp"
#include "reboot/browser/own_servers.hpp"
#include "reboot/browser/token_bucket.hpp"
#include "reboot/browser/version_match.hpp"
#include "reboot/foundation/clock.hpp"
#include "reboot/foundation/user_request.hpp"
#include "wire_mapping.hpp"

namespace reboot::browser {

namespace {

namespace wire = sb::wire;

[[nodiscard]] Diagnostic own_server() { return to_diagnostic(JoinFailure{.code = JoinFailureCode::OwnServer}); }

[[nodiscard]] Diagnostic cancelled() { return to_diagnostic(RbsbRequestError{.failure = RbsbFailure::Cancelled}); }

[[nodiscard]] Diagnostic invalid_answer() {
    return make_diag(ErrorDomain::Browser, kInvalidAnswer).kind(ErrorKind::InvalidInput).build();
}

// The grant as the game can use it; nullopt for an address that is neither 4 nor 16 bytes.
[[nodiscard]] std::optional<Endpoint> granted_endpoint(const wire::JoinGrant& grant) {
    if (grant.port == 0 || grant.port > 0xFFFF) return std::nullopt;
    const Port port{static_cast<u16>(grant.port)};
    if (grant.address.size() == 4) {
        const u32 host_order = (u32{grant.address[0]} << 24) | (u32{grant.address[1]} << 16) |
                               (u32{grant.address[2]} << 8) | u32{grant.address[3]};
        return Endpoint{IpAddress::v4(host_order), port};
    }
    if (grant.address.size() == 16) {
        IpAddress address;
        std::memcpy(address.bytes.data(), grant.address.data(), address.bytes.size());
        return Endpoint{address, port};
    }
    return std::nullopt;
}

}  // namespace

struct JoinService::Impl {
    struct Alive {
        Impl* impl = nullptr;
    };

    struct Run {
        Run(JoinRequest request_in, OperationBase& op_in, std::optional<SessionId> session_in,
            UniqueFunction<void(Result<JoinOutcome>)> done_in)
            : request(std::move(request_in)), op(op_in), session(session_in), done(std::move(done_in)) {}

        JoinRequest request;
        OperationBase& op;
        std::optional<SessionId> session;
        UniqueFunction<void(Result<JoinOutcome>)> done;
        bool finished = false;
        std::optional<ServerDetails> server;
        std::chrono::milliseconds clock_offset{0};
        std::optional<RequestId> prompt;
        CancelRegistration cancel;
    };

    using RunPtr = std::shared_ptr<Run>;

    Impl(BrowserSession& session_in, const IOwnServers& own_in, UserRequestRegistry& requests_in, OpRegistry& ops_in,
         const IClock& clock_in, JoinPasswordSource passwords_in)
        : session(session_in), own(own_in), requests(requests_in), ops(ops_in), clock(clock_in),
          passwords(std::move(passwords_in)), alive(std::make_shared<Alive>(Alive{this})) {}

    ~Impl() { alive->impl = nullptr; }

    Impl(const Impl&) = delete;
    Impl& operator=(const Impl&) = delete;

    void start(const RunPtr& run) {
        run->cancel = run->op.token().on_cancel([weak = std::weak_ptr<Run>(run)](CancelReason) {
            if (const RunPtr owner = weak.lock()) finish(owner, std::unexpected(cancelled()));
        });
        if (run->finished) return;
        run->op.progress(Progress{.phase = "resolving"});
        session.resolve(run->request.server, run->op.token(),
                        [weak = alive, run](RbsbResult<wire::ResolveResult> result) mutable {
                            if (Impl* self = weak->impl) self->on_resolved(run, std::move(result));
                        });
    }

    static void finish(const RunPtr& run, Result<JoinOutcome> result) {
        if (run->finished) return;
        run->finished = true;
        run->cancel.reset();
        run->done(std::move(result));
    }

    void fail(const RunPtr& run, JoinFailure failure) {
        failure.server = run->request.server;
        finish(run, std::unexpected(to_diagnostic(failure)));
    }

    // An edge answer that is no verdict on this server: the connection or the edge itself failed.
    void fail_request(const RunPtr& run, const RbsbRequestError& error) {
        if (error.failure == RbsbFailure::Cancelled) {
            finish(run, std::unexpected(cancelled()));
            return;
        }
        fail(run, JoinFailure{.code = JoinFailureCode::EdgeUnavailable, .cause = to_diagnostic(error)});
    }

    void on_resolved(const RunPtr& run, RbsbResult<wire::ResolveResult> result) {
        if (run->finished) return;
        if (!result) {
            if (result.error().failure == RbsbFailure::Rejected && result.error().code == wire::ErrorCode::not_found) {
                fail(run, JoinFailure{.code = JoinFailureCode::NotFound});
                return;
            }
            fail_request(run, result.error());
            return;
        }
        if (!result->details) {
            fail(run, JoinFailure{.code = JoinFailureCode::NotFound});
            return;
        }
        if (session.edge()) run->clock_offset = session.edge()->clock_offset;
        run->server = make_server_details(*result->details, run->clock_offset);
        const std::optional<GameVersion>& local = run->request.local_version;
        if (local && !same_game_version(result->details->entry.version, *local)) {
            fail(run, JoinFailure{.code = JoinFailureCode::VersionMismatch,
                                  .server_version = result->details->entry.version,
                                  .local_version = local});
            return;
        }
        if (run->request.confirmation == JoinConfirmation::Ask) {
            ask_confirmation(run);
            return;
        }
        password_or_join(run, false);
    }

    void ask_confirmation(const RunPtr& run) {
        const RequestId id = requests.ask(
            UserRequestKind::ConfirmJoin, ConfirmJoinPrompt{*run->server}, run->op.id(), run->session,
            [weak = alive, run](const std::any& answer) -> Result<void> {
                const auto* confirm = std::any_cast<ConfirmJoinAnswer>(&answer);
                if (confirm == nullptr) return std::unexpected(invalid_answer());
                Impl* self = weak->impl;
                if (self == nullptr || run->finished) return {};
                if (!confirm->accept) {
                    self->fail(run, JoinFailure{.code = JoinFailureCode::Refused});
                    return {};
                }
                run->op.progress(Progress{.phase = "joining"});
                self->password_or_join(run, false);
                return {};
            },
            run->op.token());
        if (run->finished) return;
        run->prompt = id;
        run->op.awaiting_user(id);
    }

    void password_or_join(const RunPtr& run, bool retry) {
        if (run->server->row.has_password) {
            ask_password(run, retry);
            return;
        }
        send_join(run, std::nullopt);
    }

    void ask_password(const RunPtr& run, bool retry) {
        const RequestId id = requests.ask(
            UserRequestKind::NeedsJoinPassword, NeedsJoinPasswordPrompt{run->server->row, retry}, run->op.id(),
            run->session,
            [weak = alive, run](const std::any& answer) -> Result<void> {
                if (std::any_cast<JoinPasswordProvided>(&answer) == nullptr) return std::unexpected(invalid_answer());
                Impl* self = weak->impl;
                if (self == nullptr || run->finished || !run->prompt) return {};
                std::optional<SecretString> password = self->passwords ? self->passwords(*run->prompt) : std::nullopt;
                if (!password)
                    return make_diag(ErrorDomain::Browser, kJoinPasswordMissing).kind(ErrorKind::InvalidInput).fail();
                run->op.progress(Progress{.phase = "joining"});
                self->send_join(run, std::move(password));
                return {};
            },
            run->op.token());
        if (run->finished) return;
        run->prompt = id;
        run->op.awaiting_user(id);
    }

    void send_join(const RunPtr& run, std::optional<SecretString> password) {
        const ServerId server = run->request.server;
        auto bucket = pacing.find(server);
        if (bucket == pacing.end()) bucket = pacing.emplace(server, TokenBucket(clock, kJoinRate)).first;
        if (const auto wait = bucket->second.try_take()) {
            fail(run, JoinFailure{.code = JoinFailureCode::TooManyAttempts, .retry_after = *wait});
            return;
        }
        session.join(server, std::move(password), run->op.token(),
                     [weak = alive, run](RbsbResult<wire::JoinGrant> result) mutable {
                         if (Impl* self = weak->impl) self->on_granted(run, std::move(result));
                     });
    }

    void on_granted(const RunPtr& run, RbsbResult<wire::JoinGrant> result) {
        if (run->finished) return;
        if (!result) {
            const RbsbRequestError& error = result.error();
            if (error.failure != RbsbFailure::Rejected) {
                fail_request(run, error);
                return;
            }
            switch (error.code) {
                case wire::ErrorCode::wrong_password:
                    // The server may have set a password since it was resolved.
                    run->server->row.has_password = true;
                    ask_password(run, true);
                    return;
                case wire::ErrorCode::rate_limited:
                    pacing.at(run->request.server).hold_off(error.retry_after);
                    fail(run, JoinFailure{.code = JoinFailureCode::TooManyAttempts, .retry_after = error.retry_after});
                    return;
                case wire::ErrorCode::not_found: fail(run, JoinFailure{.code = JoinFailureCode::NotFound}); return;
                case wire::ErrorCode::unavailable: fail(run, JoinFailure{.code = JoinFailureCode::Offline}); return;
                case wire::ErrorCode::unreachable: fail(run, JoinFailure{.code = JoinFailureCode::Unreachable}); return;
                default: fail_request(run, error); return;
            }
        }
        const std::optional<Endpoint> endpoint = granted_endpoint(*result);
        if (!endpoint) {
            fail_request(run, RbsbRequestError{.failure = RbsbFailure::Rejected,
                                               .code = wire::ErrorCode::internal,
                                               .message = "malformed join grant"});
            return;
        }
        if (!endpoint->address.is_v4()) {
            fail(run, JoinFailure{.code = JoinFailureCode::UnsupportedAddressFamily, .granted = endpoint});
            return;
        }
        finish(run, JoinOutcome{*run->server, *endpoint, std::move(result->ticket),
                                local_time(result->expires_ms, run->clock_offset)});
    }

    BrowserSession& session;
    const IOwnServers& own;
    UserRequestRegistry& requests;
    OpRegistry& ops;
    const IClock& clock;
    JoinPasswordSource passwords;
    std::shared_ptr<Alive> alive;
    // Mirrors the edge's per-entry join limiter.
    std::map<ServerId, TokenBucket> pacing;
};

JoinService::JoinService(BrowserSession& session, const IOwnServers& own, UserRequestRegistry& requests, OpRegistry& ops,
                         const IClock& clock, JoinPasswordSource passwords)
    : impl_(std::make_unique<Impl>(session, own, requests, ops, clock, std::move(passwords))) {}

JoinService::~JoinService() = default;

Result<void> JoinService::join(JoinRequest request, OperationBase& op, std::optional<SessionId> session,
                               UniqueFunction<void(Result<JoinOutcome>)> done) {
    if (impl_->own.owns(request.server)) return std::unexpected(own_server());
    impl_->start(std::make_shared<Impl::Run>(std::move(request), op, session, std::move(done)));
    return {};
}

Result<OpHandle> JoinService::start_join(JoinRequest request, DisconnectPolicy policy) {
    if (impl_->own.owns(request.server)) return std::unexpected(own_server());
    auto [handle, op] = impl_->ops.create<JoinOutcome>(kOpKind, policy, std::nullopt);
    Operation<JoinOutcome>* operation = &op;
    impl_->start(std::make_shared<Impl::Run>(std::move(request), op, std::nullopt, [operation](Result<JoinOutcome> result) {
        if (result) {
            operation->complete(Completed<JoinOutcome>{std::move(*result)});
        } else if (const auto reason = operation->token().reason()) {
            operation->complete(Cancelled{*reason});
        } else {
            operation->complete(Failed{std::move(result.error())});
        }
    }));
    return handle;
}

}  // namespace reboot::browser
