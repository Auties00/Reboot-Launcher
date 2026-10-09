#pragma once

#include <condition_variable>
#include <deque>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

#include "reboot/backend/backend_sessions.hpp"
#include "reboot/foundation/executor.hpp"
#include "reboot/foundation/function.hpp"
#include "reboot/ports/net.hpp"
#include "reboot/ports/runner.hpp"
#include "reboot/publish/publish_notice_sink.hpp"

namespace reboot::logging {
class ErrorRouter;
}

namespace reboot::sessions {
class SessionRegistry;
}

// Small adapters the composition root wires between packages that may not reach each other.
namespace reboot::engine {

// Windows plays natively: no runner is supported, so runtime setup and Wine preflight are refused.
class NativeOnlyRunnerPlatform final : public ports::IRunnerPlatform {
public:
    [[nodiscard]] std::vector<ports::RunnerKind> supported() const override { return {}; }
    Result<ports::RuntimeLayout> layout(ports::RunnerKind kind, const ports::RuntimeDirs& dirs) override;
    Result<void> post_extract(const NativePath& runtime_dir) override;
    Result<ports::ProcessLaunch> runner_launch(const ports::RuntimeLayout& layout, const NativePath& prefix,
                                               const NativePath& winhost_exe, ports::EnvBlock base) override;
    Result<ports::ProcessLaunch> prefix_command(const ports::RuntimeLayout& layout, const NativePath& prefix,
                                                const ports::PrefixCommand& command, ports::EnvBlock base) override;
    Result<std::optional<std::string>> runtime_setup(const ports::RuntimeLayout& layout, CancelToken token) override;
    [[nodiscard]] std::optional<UserRequestKind> pending_prerequisite() override { return std::nullopt; }
};

// BackendService's way to stop the sessions a reconfigure would cut off.
class EngineBackendSessions final : public backend::IBackendSessions {
public:
    EngineBackendSessions(sessions::SessionRegistry& sessions, Executor& strand) noexcept
        : sessions_(sessions), strand_(strand) {}

    void stop_sessions(std::vector<SessionId> sessions, UniqueFunction<void(Result<void>)> done) override;

private:
    sessions::SessionRegistry& sessions_;
    Executor& strand_;
};

// Publish notices must reach the user: they become background failures, which UIs list as notices.
class ErrorRouterPublishNotices final : public publish::IPublishNoticeSink {
public:
    explicit ErrorRouterPublishNotices(logging::ErrorRouter& errors) noexcept : errors_(errors) {}

    void on_publish_notice(const publish::PublishNotice& notice) override;

private:
    logging::ErrorRouter& errors_;
};

// Stands in for the QUIC transport when MsQuic refused to start: every connection fails with why.
class UnavailableQuicTransport final : public ports::IQuicTransport {
public:
    explicit UnavailableQuicTransport(Diagnostic reason) : reason_(std::move(reason)) {}

    Result<std::unique_ptr<ports::IQuicConnection>> open_connection(const ports::QuicConnectOptions& options,
                                                                    ports::QuicCallbacks callbacks) override;

private:
    Diagnostic reason_;
};

// Before the strand runs, startup steps that complete through an executor run their completions here.
// Thread-safe: workers post, the calling thread runs.
class WaitingExecutor final : public Executor {
public:
    void post(UniqueFunction<void()> task) override;
    // Startup work schedules nothing for later; a timed task runs as soon as it is waited for.
    void post_at(SteadyTime when, UniqueFunction<void()> task) override;

    // Runs tasks as they arrive until `done` holds.
    void run_until(UniqueFunction<bool()> done);

private:
    std::mutex mutex_;
    std::condition_variable posted_;
    std::deque<UniqueFunction<void()>> tasks_;
};

}  // namespace reboot::engine
