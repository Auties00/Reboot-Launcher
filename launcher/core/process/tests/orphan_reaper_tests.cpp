#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <memory>
#include <mutex>
#include <optional>
#include <utility>
#include <vector>

#include "reboot/foundation/cancel.hpp"
#include "reboot/foundation/clock.hpp"
#include "reboot/foundation/executor.hpp"
#include "reboot/ports/process.hpp"
#include "reboot/process/orphan_reaper.hpp"
#include "reboot/testing/scripted_process_launcher.hpp"

using namespace reboot;
using namespace reboot::process;
using namespace std::chrono_literals;

namespace {

// The strand beside a real WorkerPool: the test thread runs what the worker posted.
class WorkerStrand final : public Executor {
public:
    void post(UniqueFunction<void()> task) override {
        const std::scoped_lock lock(mutex_);
        tasks_.push_back(std::move(task));
        ready_.notify_one();
    }
    void post_at(SteadyTime, UniqueFunction<void()> task) override { post(std::move(task)); }

    template <class Done>
    void run_until(Done&& done) {
        while (!done()) {
            UniqueFunction<void()> task;
            {
                std::unique_lock lock(mutex_);
                // A bound, not a sleep: a missing reply fails the test instead of hanging it.
                REQUIRE(ready_.wait_for(lock, std::chrono::seconds{10}, [this] { return !tasks_.empty(); }));
                task = std::move(tasks_.front());
                tasks_.pop_front();
            }
            task();
        }
    }

private:
    std::mutex mutex_;
    std::condition_variable ready_;
    std::deque<UniqueFunction<void()>> tasks_;
};

// is_alive fails for one pid and kill fails for another.
class FailingLauncher final : public ports::IProcessLauncher {
public:
    Result<std::unique_ptr<ports::ChildProcess>> spawn(const ports::ProcessLaunch&) override {
        return std::unexpected(internal_bug("unused"));
    }
    Result<bool> is_alive(u32 pid, std::chrono::system_clock::time_point) override {
        if (pid == kUnreadable) return make_diag(ErrorDomain::Process, MessageId{"process.test_unreadable"}).fail();
        return true;
    }
    Result<void> kill(u32, std::chrono::system_clock::time_point) override {
        return make_diag(ErrorDomain::Process, MessageId{"process.test_denied"}).fail();
    }

    static constexpr u32 kUnreadable = 7;
};

ChildRecord record(u32 pid, std::chrono::system_clock::time_point created) {
    return ChildRecord{.pid = pid, .created = created, .role = ChildRole::GameServer, .session = SessionId{}};
}

struct Fixture {
    Result<OrphanReapReport> reap(ports::IProcessLauncher& target, std::vector<ChildRecord> recorded,
                                  CancelToken token = {}) {
        OrphanReaper reaper(target, workers, strand);
        std::optional<Result<OrphanReapReport>> out;
        reaper.reap(std::move(recorded), std::move(token), [&](Result<OrphanReapReport> report) { out = std::move(report); });
        strand.run_until([&] { return out.has_value(); });
        return std::move(*out);
    }

    ManualClock clock;
    ManualExecutor io{clock};
    testing::ScriptedProcessLauncher launcher{io, clock, testing::FakeOs::Linux};
    WorkerStrand strand;
    WorkerPool workers{1};
};

const auto kT0 = std::chrono::system_clock::time_point{} + std::chrono::hours{1000};

}  // namespace

TEST_CASE("only processes whose pid and creation time match are killed", "[process][reaper]") {
    Fixture f;
    f.launcher.add_orphan(100, kT0);
    f.launcher.add_orphan(200, kT0);
    f.launcher.add_orphan(300, kT0 + 5s);

    Result<OrphanReapReport> report =
        f.reap(f.launcher, {record(100, kT0), record(300, kT0), record(400, kT0), record(200, kT0)});
    REQUIRE(report.has_value());
    REQUIRE(report->killed.size() == 2);
    CHECK(report->killed[0].pid == 100);
    CHECK(report->killed[1].pid == 200);
    REQUIRE(report->gone.size() == 2);
    // 300 is alive but was started at another time: a reused pid, never touched.
    CHECK(report->gone[0].pid == 300);
    CHECK(report->gone[1].pid == 400);
    CHECK(report->failed.empty());
    CHECK(f.launcher.killed() == std::vector<u32>{100, 200});
    CHECK(f.launcher.is_alive(300, kT0 + 5s).value());
}

TEST_CASE("an empty record list reports nothing", "[process][reaper]") {
    Fixture f;
    Result<OrphanReapReport> report = f.reap(f.launcher, {});
    REQUIRE(report.has_value());
    CHECK(report->killed.empty());
    CHECK(report->gone.empty());
    CHECK(report->failed.empty());
}

TEST_CASE("a process that cannot be checked or killed stays recorded as failed", "[process][reaper]") {
    Fixture f;
    FailingLauncher failing;
    Result<OrphanReapReport> report = f.reap(failing, {record(FailingLauncher::kUnreadable, kT0), record(8, kT0)});
    REQUIRE(report.has_value());
    CHECK(report->killed.empty());
    CHECK(report->gone.empty());
    REQUIRE(report->failed.size() == 2);
    CHECK(report->failed[0].first.pid == FailingLauncher::kUnreadable);
    CHECK(report->failed[0].second.id == "process.reap_failed");
    REQUIRE(report->failed[0].second.causes.size() == 1);
    CHECK(report->failed[0].second.causes[0].id == "process.test_unreadable");
    CHECK(report->failed[1].first.pid == 8);
    REQUIRE(report->failed[1].second.causes.size() == 1);
    CHECK(report->failed[1].second.causes[0].id == "process.test_denied");
}

TEST_CASE("after cancellation every unchecked record fails with reap_cancelled", "[process][reaper]") {
    Fixture f;
    f.launcher.add_orphan(100, kT0);
    CancelSource source;
    source.cancel(CancelReason::Shutdown);
    Result<OrphanReapReport> report = f.reap(f.launcher, {record(100, kT0), record(200, kT0)}, source.token());
    REQUIRE(report.has_value());
    CHECK(report->killed.empty());
    REQUIRE(report->failed.size() == 2);
    CHECK(report->failed[0].second.id == "process.reap_cancelled");
    CHECK(report->failed[0].second.kind == ErrorKind::Cancelled);
    CHECK(report->failed[1].first.pid == 200);
    CHECK(f.launcher.killed().empty());
    CHECK(f.launcher.is_alive(100, kT0).value());
}

TEST_CASE("done runs on the strand, not on the worker", "[process][reaper]") {
    Fixture f;
    f.launcher.add_orphan(100, kT0);
    OrphanReaper reaper(f.launcher, f.workers, f.strand);
    std::optional<Result<OrphanReapReport>> out;
    reaper.reap({record(100, kT0)}, {}, [&](Result<OrphanReapReport> report) { out = std::move(report); });
    CHECK_FALSE(out.has_value());
    f.strand.run_until([&] { return out.has_value(); });
    REQUIRE(out->has_value());
    CHECK((*out)->killed.size() == 1);
}
