#include "engine_adapters.hpp"

#include <memory>
#include <utility>

#include "messages.hpp"
#include "reboot/foundation/log.hpp"
#include "reboot/logging/error_router.hpp"
#include "reboot/publish/publish_notice.hpp"
#include "reboot/sessions/session_registry.hpp"
#include "reboot/sessions/stop_request.hpp"

namespace rb::engine {

namespace {

[[nodiscard]] std::unexpected<Diagnostic> no_runner() {
    return make_diag(ErrorDomain::Engine, msg::kNoWineRunner).kind(ErrorKind::Unsupported).fail();
}

}  // namespace

Result<ports::RuntimeLayout> NativeOnlyRunnerPlatform::layout(ports::RunnerKind, const ports::RuntimeDirs&) {
    return no_runner();
}

Result<void> NativeOnlyRunnerPlatform::post_extract(const NativePath&) { return no_runner(); }

Result<ports::ProcessLaunch> NativeOnlyRunnerPlatform::runner_launch(const ports::RuntimeLayout&, const NativePath&,
                                                                     const NativePath&, ports::EnvBlock) {
    return no_runner();
}

Result<ports::ProcessLaunch> NativeOnlyRunnerPlatform::prefix_command(const ports::RuntimeLayout&, const NativePath&,
                                                                      const ports::PrefixCommand&, ports::EnvBlock) {
    return no_runner();
}

Result<std::optional<std::string>> NativeOnlyRunnerPlatform::runtime_setup(const ports::RuntimeLayout&, CancelToken) {
    return no_runner();
}

void EngineBackendSessions::stop_sessions(std::vector<SessionId> sessions, UniqueFunction<void(Result<void>)> done) {
    struct Join {
        std::size_t left = 0;
        UniqueFunction<void(Result<void>)> done;
    };
    auto join = std::make_shared<Join>();
    join->left = sessions.size() + 1;
    join->done = std::move(done);
    auto ended = [join] {
        if (--join->left == 0) join->done(Result<void>{});
    };
    for (const SessionId& session : sessions) {
        sessions::StopRequest stop;
        stop.reason = sessions::StopReason::User;
        // A session that already ended counts as stopped.
        if (Result<void> stopping = sessions_.stop(session, std::move(stop), ended); !stopping) ended();
    }
    strand_.post(ended);
}

void ErrorRouterPublishNotices::on_publish_notice(const publish::PublishNotice& notice) {
    errors_.report(notice.message, LogCategory::Host, notice.session);
}

Result<std::unique_ptr<ports::IQuicConnection>> UnavailableQuicTransport::open_connection(const ports::QuicConnectOptions&,
                                                                                          ports::QuicCallbacks) {
    return std::unexpected(reason_);
}

void WaitingExecutor::post(UniqueFunction<void()> task) {
    {
        const std::scoped_lock lock(mutex_);
        tasks_.push_back(std::move(task));
    }
    posted_.notify_one();
}

void WaitingExecutor::post_at(SteadyTime, UniqueFunction<void()> task) { post(std::move(task)); }

void WaitingExecutor::run_until(UniqueFunction<bool()> done) {
    while (!done()) {
        UniqueFunction<void()> task;
        {
            std::unique_lock lock(mutex_);
            posted_.wait(lock, [this] { return !tasks_.empty(); });
            task = std::move(tasks_.front());
            tasks_.pop_front();
        }
        task();
    }
}

}  // namespace rb::engine
