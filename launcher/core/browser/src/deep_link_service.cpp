#include "reboot/browser/deep_link_service.hpp"

#include <any>
#include <utility>

#include "messages.hpp"
#include "reboot/browser/browser_session.hpp"
#include "reboot/browser/deep_link.hpp"
#include "reboot/browser/game_server_target.hpp"
#include "reboot/browser/join_outcome.hpp"
#include "reboot/browser/join_prompts.hpp"
#include "reboot/browser/own_servers.hpp"
#include "reboot/foundation/user_request.hpp"

namespace reboot::browser {

namespace {

namespace wire = sb::wire;

}  // namespace

struct DeepLinkService::Impl {
    struct Alive {
        Impl* impl = nullptr;
    };

    struct Run {
        explicit Run(Operation<LinkResolution>& op_in) : op(op_in) {}

        Operation<LinkResolution>& op;
        bool finished = false;
        CancelRegistration cancel;
    };

    using RunPtr = std::shared_ptr<Run>;

    Impl(BrowserSession& session_in, GameServerTarget& target_in, const IOwnServers& own_in,
         UserRequestRegistry& requests_in, OpRegistry& ops_in)
        : session(session_in), target(target_in), own(own_in), requests(requests_in), ops(ops_in),
          alive(std::make_shared<Alive>(Alive{this})) {}

    ~Impl() { alive->impl = nullptr; }

    Impl(const Impl&) = delete;
    Impl& operator=(const Impl&) = delete;

    void finish(const RunPtr& run, Outcome<LinkResolution> outcome) {
        if (run->finished) return;
        run->finished = true;
        run->cancel.reset();
        if (pending == run->op.id()) pending.reset();
        run->op.complete(std::move(outcome));
    }

    void on_resolved(const RunPtr& run, RbsbResult<wire::ResolveResult> result) {
        if (run->finished) return;
        if (!result) {
            const RbsbRequestError& error = result.error();
            if (error.failure == RbsbFailure::Cancelled) {
                finish(run, Cancelled{run->op.token().reason().value_or(CancelReason::User)});
            } else if (error.failure == RbsbFailure::Rejected && error.code == wire::ErrorCode::not_found) {
                finish(run, Failed{link_not_found()});
            } else {
                finish(run, Failed{to_diagnostic(error)});
            }
            return;
        }
        if (!result->details) {
            finish(run, Failed{link_not_found()});
            return;
        }
        const std::chrono::milliseconds offset = session.edge() ? session.edge()->clock_offset : std::chrono::milliseconds{0};
        ServerDetails server = make_server_details(*result->details, offset);
        const RequestId id = requests.ask(
            UserRequestKind::ConfirmJoin, ConfirmJoinPrompt{server}, run->op.id(), std::nullopt,
            [weak = alive, run, server](const std::any& answer) -> Result<void> {
                const auto* confirm = std::any_cast<ConfirmJoinAnswer>(&answer);
                if (confirm == nullptr)
                    return make_diag(ErrorDomain::Browser, kInvalidAnswer).kind(ErrorKind::InvalidInput).fail();
                Impl* self = weak->impl;
                if (self == nullptr || run->finished) return {};
                if (!confirm->accept) {
                    self->finish(run, Failed{make_diag(ErrorDomain::Browser, kJoinRefused).kind(ErrorKind::Cancelled).build()});
                    return {};
                }
                self->target.set_server(ServerTarget{server.row.id, server.row.name, server.row.author});
                self->finish(run, Completed<LinkResolution>{LinkResolution{server}});
                return {};
            },
            run->op.token());
        if (!run->finished) run->op.awaiting_user(id);
    }

    [[nodiscard]] static Diagnostic link_not_found() {
        return make_diag(ErrorDomain::Browser, kLinkNotFound).kind(ErrorKind::NotFound).build();
    }

    BrowserSession& session;
    GameServerTarget& target;
    const IOwnServers& own;
    UserRequestRegistry& requests;
    OpRegistry& ops;
    std::shared_ptr<Alive> alive;
    // The newest link's op; a newer link cancels it.
    std::optional<OpId> pending;
};

DeepLinkService::DeepLinkService(BrowserSession& session, GameServerTarget& target, const IOwnServers& own,
                                 UserRequestRegistry& requests, OpRegistry& ops)
    : impl_(std::make_unique<Impl>(session, target, own, requests, ops)) {}

DeepLinkService::~DeepLinkService() = default;

Result<OpHandle> DeepLinkService::start_resolve(std::string_view link, DisconnectPolicy policy) {
    Result<DeepLink> parsed = parse_deep_link(link);
    if (!parsed) return std::unexpected(std::move(parsed.error()));
    const ServerId server = parsed->server;
    if (impl_->own.owns(server)) return std::unexpected(to_diagnostic(JoinFailure{.code = JoinFailureCode::OwnServer}));

    if (const std::optional<OpId> previous = std::exchange(impl_->pending, std::nullopt))
        (void)impl_->ops.cancel(*previous, CancelReason::Superseded);

    auto [handle, op] = impl_->ops.create<LinkResolution>(kOpKind, policy, std::nullopt);
    impl_->pending = handle.id();
    auto run = std::make_shared<Impl::Run>(op);
    run->cancel = op.token().on_cancel([weak = impl_->alive, run_weak = std::weak_ptr<Impl::Run>(run)](CancelReason reason) {
        Impl* self = weak->impl;
        const Impl::RunPtr owner = run_weak.lock();
        if (self != nullptr && owner) self->finish(owner, Cancelled{reason});
    });
    op.progress(Progress{.phase = "resolving"});
    impl_->session.resolve(server, op.token(), [weak = impl_->alive, run](RbsbResult<wire::ResolveResult> result) mutable {
        if (Impl* self = weak->impl) self->on_resolved(run, std::move(result));
    });
    return handle;
}

}  // namespace reboot::browser
