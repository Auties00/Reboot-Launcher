#pragma once

#include <any>
#include <chrono>
#include <deque>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "reboot/backend/backend_event.hpp"
#include "reboot/backend/backend_process.hpp"
#include "reboot/backend/backend_process_observer.hpp"
#include "reboot/backend/backend_session_config.hpp"
#include "reboot/foundation/events.hpp"
#include "reboot/foundation/log.hpp"
#include "reboot/testing/deterministic_runtime.hpp"
#include "reboot/testing/fake_backend.hpp"
#include "reboot/testing/fake_platform_paths.hpp"
#include "reboot/testing/scripted_process_launcher.hpp"

namespace rb::backend::test {

inline constexpr std::string_view kBackendExe = "reboot-backend";

[[nodiscard]] inline NativePath backend_data_dir() { return testing::default_fake_root() / "data" / "backend"; }

[[nodiscard]] inline process::ProcessSpec backend_spec() {
    process::ProcessSpec spec;
    spec.role = process::ChildRole::Backend;
    spec.exe = testing::default_fake_root() / "bin" / std::string(kBackendExe);
    spec.args = {"--control=stdio"};
    spec.cwd = backend_data_dir();
    return spec;
}

[[nodiscard]] inline SessionId session_id(u8 tag) {
    Uuid uuid{};
    uuid.bytes[15] = tag;
    return SessionId{uuid};
}

[[nodiscard]] inline AccountRecordId record_id(u8 tag) {
    Uuid uuid{};
    uuid.bytes[0] = tag;
    return AccountRecordId{uuid};
}

[[nodiscard]] inline BackendSessionConfig session_config(std::string account_id, std::string key) {
    BackendSessionConfig config;
    config.origin = SecretString("http://127.0.0.1:4000/s/" + key + "/");
    config.session_key = SecretString(std::move(key));
    config.account_id = std::move(account_id);
    config.version = GameVersion{12, 41, std::nullopt};
    config.changelist = Changelist{12345};
    return config;
}

struct ProcessObserver final : IBackendProcessObserver {
    void on_ready(const BackendReady& ready) override { readies.push_back(ready); }
    void on_exit(const process::ChildExitInfo& exit) override { exits.push_back(exit); }
    void on_login_observed(const LoginObservedEvent& event) override { logins.push_back(event); }

    std::vector<BackendReady> readies;
    std::vector<process::ChildExitInfo> exits;
    std::vector<LoginObservedEvent> logins;
};

// Every BackendStateChanged as published, before coalescing can merge two of them.
class BackendEvents {
public:
    explicit BackendEvents(EventBus& bus)
        : subscription_(bus.subscribe(EventFilter{{EventKind::BackendStateChanged}, std::nullopt, std::nullopt}, 1u << 20)) {
        subscription_->set_notify([this] {
            std::vector<EventEnvelope> drained;
            subscription_->drain(drained, 64);
            for (const EventEnvelope& event : drained) events_.push_back(std::any_cast<BackendEvent>(event.payload));
        });
    }

    [[nodiscard]] const std::vector<BackendEvent>& all() const noexcept { return events_; }
    [[nodiscard]] std::vector<BackendChange> changes() const {
        std::vector<BackendChange> out;
        for (const BackendEvent& event : events_) out.push_back(event.change);
        return out;
    }
    [[nodiscard]] bool saw(BackendChange change) const {
        for (const BackendEvent& event : events_)
            if (event.change == change) return true;
        return false;
    }
    void clear() { events_.clear(); }

private:
    std::shared_ptr<Subscription> subscription_;
    std::vector<BackendEvent> events_;
};

// A BackendProcess over ScriptedProcessLauncher whose spawns each run a FakeBackend.
struct ProcessHarness {
    ProcessHarness() {
        // Unix time 0 reads as "never logged in" on the wire.
        rt.clock().set_system(std::chrono::system_clock::time_point{std::chrono::hours{24 * 365 * 50}});
        launcher.serve_exe(kBackendExe, [this](const ports::ProcessLaunch&) {
            testing::FakeBackendScript next = script;
            if (!scripts.empty()) {
                next = std::move(scripts.front());
                scripts.pop_front();
            }
            auto peer = std::make_unique<testing::FakeBackend>(rt.strand(), rt.clock(), std::move(next));
            backends.push_back(peer.get());
            return std::unique_ptr<testing::IStdioPeer>(std::move(peer));
        });
        process = std::make_unique<BackendProcess>(
            launcher, rt.strand(), rt.timers(), rt.clock(), redactor, backend_spec(),
            [this](const process::ChildRecord& record, process::RecordChange change) { records.emplace_back(record, change); },
            LogLevel::Info);
    }

    [[nodiscard]] testing::FakeBackend& backend() {
        REQUIRE_FALSE(backends.empty());
        return *backends.back();
    }

    testing::DeterministicRuntime rt;
    testing::ScriptedProcessLauncher launcher{rt.strand(), rt.clock(), testing::FakeOs::Linux};
    Redactor redactor;
    // What every spawn runs, unless `scripts` names one for it.
    testing::FakeBackendScript script;
    std::deque<testing::FakeBackendScript> scripts;
    std::vector<testing::FakeBackend*> backends;
    std::vector<std::pair<process::ChildRecord, process::RecordChange>> records;
    std::unique_ptr<BackendProcess> process;
};

template <class T>
struct Captured {
    std::optional<T> value;
    int calls = 0;

    auto sink() {
        return [this](T result) {
            ++calls;
            value = std::move(result);
        };
    }
};

}  // namespace rb::backend::test
