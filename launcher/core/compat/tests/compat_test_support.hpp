#pragma once

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <condition_variable>
#include <deque>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <mutex>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

#include "reboot/foundation/clock.hpp"
#include "reboot/foundation/diag.hpp"
#include "reboot/foundation/executor.hpp"
#include "reboot/foundation/random.hpp"
#include "reboot/ports/file_system.hpp"
#include "reboot/testing/fault_plan.hpp"
#include "reboot/testing/in_memory_file_system.hpp"
#include "reboot/testing/scratch_dir.hpp"

namespace reboot::compat::test {

[[nodiscard]] inline std::string arg_text(const Diagnostic& diag, std::string_view name) {
    const Arg* arg = diag.find_arg(name);
    if (arg == nullptr) return {};
    if (const auto* text = std::get_if<std::string>(arg)) return *text;
    if (const auto* path = std::get_if<WirePath>(arg)) return path->display;
    return {};
}

// The strand beside real worker threads, on manual time: only the test thread runs tasks.
class TestStrand final : public Executor {
public:
    explicit TestStrand(ManualClock& clock) : clock_(clock) {}

    void post(UniqueFunction<void()> task) override {
        const std::scoped_lock lock(mutex_);
        ready_.push_back(std::move(task));
        posted_.notify_one();
    }

    void post_at(SteadyTime when, UniqueFunction<void()> task) override {
        const std::scoped_lock lock(mutex_);
        timed_.emplace(when, std::move(task));
    }

    // Runs tasks, waiting for workers to post, until `done` holds. A bound, not a sleep: a missing
    // post fails the test instead of hanging it.
    template <class Done>
    void run_until(Done&& done) {
        while (!done()) {
            UniqueFunction<void()> task = next(true);
            REQUIRE(static_cast<bool>(task));
            task();
        }
    }

    // Runs what is ready now and what it posts, waiting for nothing.
    void drain() {
        while (UniqueFunction<void()> task = next(false)) task();
    }

    void advance(std::chrono::steady_clock::duration by) {
        clock_.advance(by);
        drain();
    }

private:
    UniqueFunction<void()> next(bool wait) {
        std::unique_lock lock(mutex_);
        const SteadyTime now = clock_.steady_now();
        while (!timed_.empty() && timed_.begin()->first <= now) {
            ready_.push_back(std::move(timed_.begin()->second));
            timed_.erase(timed_.begin());
        }
        if (wait && !posted_.wait_for(lock, std::chrono::seconds{20}, [this] { return !ready_.empty(); })) return {};
        if (ready_.empty()) return {};
        UniqueFunction<void()> task = std::move(ready_.front());
        ready_.pop_front();
        return task;
    }

    ManualClock& clock_;
    std::mutex mutex_;
    std::condition_variable posted_;
    std::deque<UniqueFunction<void()>> ready_;
    std::multimap<SteadyTime, UniqueFunction<void()>> timed_;
};

inline constexpr MessageId kDiskError{"compat_test.disk_error"};

// IFileSystem over a real scratch directory, for the prefix trees PrefixManager walks with
// std::filesystem. Faults use the in-memory fake's operation names.
class DiskFileSystem final : public ports::IFileSystem {
public:
    Result<void> atomic_replace(const NativePath& target, std::span<const u8> bytes, bool) override {
        if (auto fault = faults_.take(testing::FsOperation::AtomicReplace)) return std::unexpected(std::move(*fault));
        std::ofstream out(target, std::ios::binary | std::ios::trunc);
        out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
        if (!out) return fail(target);
        return {};
    }

    Result<std::vector<u8>> read_all(const NativePath& path) override {
        if (auto fault = faults_.take(testing::FsOperation::ReadAll)) return std::unexpected(std::move(*fault));
        std::ifstream in(path, std::ios::binary);
        if (!in) return fail(path);
        return std::vector<u8>(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    }

    Result<ports::FileLock> lock_exclusive(const NativePath&, bool) override { return ports::FileLock{}; }
    Result<void> restrict_to_owner(const NativePath&) override { return {}; }
    Result<ports::HeldFile> open_deny_write(const NativePath& path) override { return fail(path); }
    Result<ports::FileRevision> revision(const NativePath& path) override { return fail(path); }
    Result<ports::SharedRead> read_shared(const NativePath& path, u64, std::size_t) override { return fail(path); }

    Result<void> create_dirs_owner_only(const NativePath& path) override {
        if (auto fault = faults_.take(testing::FsOperation::CreateDirsOwnerOnly)) return std::unexpected(std::move(*fault));
        std::error_code error;
        std::filesystem::create_directories(path, error);
        if (error) return fail(path);
        return {};
    }

    Result<void> remove_tree(const NativePath& path) override {
        if (auto fault = faults_.take(testing::FsOperation::RemoveTree)) return std::unexpected(std::move(*fault));
        if (path == refused_removal) return fail(path);
        std::error_code error;
        std::filesystem::remove_all(path, error);
        if (error) return fail(path);
        return {};
    }

    [[nodiscard]] testing::FaultPlan<testing::FsOperation>& faults() noexcept { return faults_; }

    // remove_tree of exactly this path fails.
    NativePath refused_removal;

private:
    [[nodiscard]] static std::unexpected<Diagnostic> fail(const NativePath& path) {
        return std::unexpected(make_diag(ErrorDomain::Internal, kDiskError).arg("path", path).build());
    }

    testing::FaultPlan<testing::FsOperation> faults_;
};

// OS randomness: ctest runs the cases of this binary in parallel processes.
[[nodiscard]] inline testing::ScratchDir make_scratch() {
    OsRandom random;
    Result<testing::ScratchDir> dir = testing::ScratchDir::create(random, "reboot-compat");
    REQUIRE(dir);
    return std::move(*dir);
}

inline void write_file(const NativePath& path, std::string_view text) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out.write(text.data(), static_cast<std::streamsize>(text.size()));
}

[[nodiscard]] inline std::string read_file(const NativePath& path) {
    std::ifstream in(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

}  // namespace reboot::compat::test
