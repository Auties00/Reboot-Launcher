#include <chrono>
#include <memory>
#include <mutex>
#include <optional>
#include <utility>
#include <vector>

#include "reboot/testing/fake_session_control.hpp"
#include "reboot/testing/fake_session_host.hpp"
#include "reboot/testing/scripted_process_launcher.hpp"

namespace rb::testing {
namespace {

// What the control and the engine's IGameSession share; posted events hold it too.
struct SessionLink {
    explicit SessionLink(UniqueFunction<void(ports::SessionHostEvent)> callback) : on_event(std::move(callback)) {}

    std::mutex mutex;
    UniqueFunction<void(ports::SessionHostEvent)> on_event;
    bool released = false;
    bool game_exited = false;

    // Set by the control, for what the engine's handle does to it.
    UniqueFunction<Result<void>(const ports::InjectEntry&)> inject;
    UniqueFunction<Result<void>()> resume;
    UniqueFunction<void(std::chrono::milliseconds)> stop;
    UniqueFunction<void()> release;
};

class GameSessionHandle final : public ports::IGameSession {
public:
    explicit GameSessionHandle(std::shared_ptr<SessionLink> link) : link_(std::move(link)) {}
    ~GameSessionHandle() override {
        {
            const std::scoped_lock lock(link_->mutex);
            link_->released = true;
        }
        if (link_->release) link_->release();
    }
    GameSessionHandle(const GameSessionHandle&) = delete;
    GameSessionHandle& operator=(const GameSessionHandle&) = delete;

    Result<void> inject(const ports::InjectEntry& entry) override {
        if (!link_->inject) return {};
        return link_->inject(entry);
    }

    Result<void> resume() override {
        if (!link_->resume) return {};
        return link_->resume();
    }

    void stop(std::chrono::milliseconds grace) override {
        if (link_->stop) link_->stop(grace);
    }

private:
    std::shared_ptr<SessionLink> link_;
};

}  // namespace

struct FakeSessionControl::Delivery : SessionLink {
    using SessionLink::SessionLink;
};

FakeSessionControl::FakeSessionControl(FakeSessionHost& host, Executor& io, ports::SessionLaunch launch,
                                       UniqueFunction<void(ports::SessionHostEvent)> on_event)
    : host_(host), io_(io), launch_(std::move(launch)), delivery_(std::make_shared<Delivery>(std::move(on_event))) {
    SessionLink& link = *delivery_;
    link.inject = [this](const ports::InjectEntry& entry) -> Result<void> {
        if (auto error = host_.faults().take(SessionHostOperation::Inject)) return std::unexpected(std::move(*error));
        injected_.push_back(entry);
        emit(ports::Injected{entry.path, true, std::nullopt});
        return {};
    };
    link.resume = [this]() -> Result<void> {
        if (auto error = host_.faults().take(SessionHostOperation::Resume)) return std::unexpected(std::move(*error));
        resumed_ = true;
        if (host_.on_resume_) host_.on_resume_(*this);
        return {};
    };
    link.stop = [this](std::chrono::milliseconds grace) {
        stop_grace_ = grace;
        game_exits(kJobKillExitCode);
    };
    link.release = [this] { released_ = true; };
}

FakeSessionControl::~FakeSessionControl() {
    UniqueFunction<Result<void>(const ports::InjectEntry&)> inject = std::move(delivery_->inject);
    UniqueFunction<Result<void>()> resume = std::move(delivery_->resume);
    UniqueFunction<void(std::chrono::milliseconds)> stop = std::move(delivery_->stop);
    UniqueFunction<void()> release = std::move(delivery_->release);
}

std::unique_ptr<ports::IGameSession> FakeSessionControl::make_handle() {
    return std::make_unique<GameSessionHandle>(delivery_);
}

Result<GameControlBootstrap> FakeSessionControl::bootstrap() const { return read_game_control_bootstrap(launch_.env); }

void FakeSessionControl::emit(ports::SessionHostEvent event) {
    io_.post([link = std::shared_ptr<SessionLink>(delivery_), event = std::move(event)]() mutable {
        {
            const std::scoped_lock lock(link->mutex);
            if (link->released || !link->on_event) return;
        }
        link->on_event(std::move(event));
    });
}

void FakeSessionControl::spawn_all(u32 first_pid) {
    emit(ports::Spawned{ports::SessionRole::Game, first_pid});
    u32 pid = first_pid;
    for (std::size_t i = 0; i < launch_.companions.size(); ++i) emit(ports::Spawned{ports::SessionRole::Companion, ++pid});
}

void FakeSessionControl::game_exits(std::optional<int> code) {
    {
        const std::scoped_lock lock(delivery_->mutex);
        if (delivery_->game_exited) return;
        delivery_->game_exited = true;
    }
    emit(ports::Exited{ports::SessionRole::Game, code});
}

Result<std::unique_ptr<ports::IGameSession>> FakeSessionHost::launch(const ports::SessionLaunch& launch,
                                                                    UniqueFunction<void(ports::SessionHostEvent)> on_event) {
    if (auto error = faults_.take(SessionHostOperation::Launch)) return std::unexpected(std::move(*error));
    auto control = std::make_unique<FakeSessionControl>(*this, io_, launch, std::move(on_event));
    std::unique_ptr<ports::IGameSession> handle = control->make_handle();
    FakeSessionControl& added = *sessions_.emplace_back(std::move(control));
    if (on_launch_) on_launch_(added);
    return handle;
}

void FakeSessionHost::on_launch(UniqueFunction<void(FakeSessionControl&)> hook) { on_launch_ = std::move(hook); }

void FakeSessionHost::on_resume(UniqueFunction<void(FakeSessionControl&)> hook) { on_resume_ = std::move(hook); }

std::vector<FakeSessionControl*> FakeSessionHost::sessions() const {
    std::vector<FakeSessionControl*> out;
    out.reserve(sessions_.size());
    for (const auto& session : sessions_) out.push_back(session.get());
    return out;
}

FakeSessionControl* FakeSessionHost::last() const { return sessions_.empty() ? nullptr : sessions_.back().get(); }

}  // namespace rb::testing
