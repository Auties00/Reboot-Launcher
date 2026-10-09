#include "reboot/play/play_service.hpp"

#include <utility>

#include "deps_env.hpp"
#include "play_core.hpp"

namespace reboot::play {

struct PlayService::Impl {
    Impl(PlayServiceDeps deps_in, PlayServiceOptions options)
        : deps(std::move(deps_in)),
          env(deps, options.daemon_env),
          core(PlayCoreDeps{.env = env,
                            .sessions = deps.sessions,
                            .channel = deps.channel,
                            .match_targets = deps.match_targets,
                            .system = deps.system,
                            .random = deps.random,
                            .redactor = deps.redactor,
                            .requests = deps.requests,
                            .ops = deps.ops,
                            .events = deps.events,
                            .strand = deps.strand},
               std::move(options)) {}

    PlayServiceDeps deps;
    DepsEnv env;
    // Declared last: destroyed first, while the services its sessions release into still exist.
    PlayCore core;
};

PlayService::PlayService(PlayServiceDeps deps, PlayServiceOptions options)
    : impl_(std::make_unique<Impl>(std::move(deps), std::move(options))) {}

PlayService::~PlayService() = default;

Result<PlayPlan> PlayService::plan(const PlayRequest& request) const { return impl_->core.plan(request); }

Result<OpHandle> PlayService::start(PlayRequest request, DisconnectPolicy policy) {
    return impl_->core.start(std::move(request), policy);
}

void PlayService::on_login_observed(const backend::LoginObservedEvent& event) { impl_->core.on_login_observed(event); }

std::optional<PlaySessionState> PlayService::state(SessionId session) const { return impl_->core.state(session); }

}  // namespace reboot::play
