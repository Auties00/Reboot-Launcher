#pragma once

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <deque>
#include <expected>
#include <filesystem>
#include <functional>
#include <fstream>
#include <iterator>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <set>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_tostring.hpp>

#include "reboot/foundation/clock.hpp"
#include "reboot/foundation/diag.hpp"
#include "reboot/foundation/executor.hpp"
#include "reboot/foundation/function.hpp"
#include "reboot/foundation/types.hpp"
#include "reboot/ports/file_system.hpp"

namespace rb::engine::test {

// The strand beside real worker and I/O threads: they post from their threads, timed tasks follow
// the ManualClock, and only the test thread runs anything.
class TestStrand final : public Executor {
public:
    explicit TestStrand(ManualClock& clock) : clock_(clock) {}

    // pump_until then moves time only while `workers` is idle, so a slow job never meets a deadline it would beat.
    void hold_time_for(const WorkerPool& workers) { workers_ = &workers; }

    void post(UniqueFunction<void()> task) override {
        const std::scoped_lock lock(mutex_);
        ready_.push_back(std::move(task));
        posted_.notify_one();
    }
    void post_at(SteadyTime when, UniqueFunction<void()> task) override {
        const std::scoped_lock lock(mutex_);
        timed_.emplace(when, std::move(task));
        posted_.notify_one();
    }

    std::size_t run_ready() {
        std::size_t ran = 0;
        while (UniqueFunction<void()> task = next(false)) {
            task();
            ++ran;
        }
        return ran;
    }

    // Runs tasks, waiting for other threads to post, until `done` holds.
    template <class Done>
    void run_until(Done&& done) {
        run_ready();
        while (!done()) {
            UniqueFunction<void()> task = next(true);
            REQUIRE(static_cast<bool>(task));
            task();
            run_ready();
        }
    }

    // Like run_until, but gives up quietly; for waits whose end the test checks itself.
    template <class Done>
    bool try_run_until(Done&& done, std::chrono::milliseconds budget = std::chrono::seconds{20}) {
        run_ready();
        const auto give_up = std::chrono::steady_clock::now() + budget;
        while (!done()) {
            if (std::chrono::steady_clock::now() > give_up) return false;
            if (UniqueFunction<void()> task = next(true, std::chrono::milliseconds{50})) task();
            run_ready();
        }
        return true;
    }

    // Runs until `done`, letting other threads post; when nothing has arrived for a while and no
    // held-for worker is busy, time jumps to the next timer, so a wait that a deadline or a
    // debounce ends still ends. Gives up after `simulated` of jumps or `budget` of real time.
    template <class Done>
    bool pump_until(Done&& done, std::chrono::steady_clock::duration simulated = std::chrono::minutes{30},
                    std::chrono::milliseconds budget = std::chrono::seconds{60}) {
        const SteadyTime limit = clock_.steady_now() + simulated;
        const auto give_up = std::chrono::steady_clock::now() + budget;
        run_ready();
        while (!done()) {
            if (std::chrono::steady_clock::now() > give_up) return false;
            if (UniqueFunction<void()> task = next(true, std::chrono::milliseconds{30})) {
                task();
                run_ready();
                continue;
            }
            if (workers_ != nullptr && !workers_->idle()) continue;
            std::optional<SteadyTime> due;
            {
                const std::scoped_lock lock(mutex_);
                if (!timed_.empty()) due = timed_.begin()->first;
            }
            if (!due || *due > limit) continue;
            if (*due > clock_.steady_now()) clock_.advance(*due - clock_.steady_now());
            run_ready();
        }
        return true;
    }

    // Moves time one due task at a time, so each timer runs at its own deadline.
    void advance(std::chrono::steady_clock::duration by) {
        const SteadyTime end = clock_.steady_now() + by;
        run_ready();
        while (true) {
            SteadyTime due{};
            {
                const std::scoped_lock lock(mutex_);
                if (timed_.empty() || timed_.begin()->first > end) break;
                due = timed_.begin()->first;
            }
            if (due > clock_.steady_now()) clock_.advance(due - clock_.steady_now());
            run_ready();
        }
        if (end > clock_.steady_now()) clock_.advance(end - clock_.steady_now());
        run_ready();
    }

private:
    UniqueFunction<void()> next(bool wait, std::chrono::milliseconds wait_for = std::chrono::seconds{20}) {
        std::unique_lock lock(mutex_);
        const auto promote = [this] {
            const SteadyTime now = clock_.steady_now();
            while (!timed_.empty() && timed_.begin()->first <= now) {
                ready_.push_back(std::move(timed_.begin()->second));
                timed_.erase(timed_.begin());
            }
        };
        promote();
        // A bound, not a sleep: a missing post fails the test instead of hanging it.
        if (wait && ready_.empty()) {
            posted_.wait_for(lock, wait_for, [&] {
                promote();
                return !ready_.empty();
            });
        }
        if (ready_.empty()) return {};
        UniqueFunction<void()> task = std::move(ready_.front());
        ready_.pop_front();
        return task;
    }

    ManualClock& clock_;
    const WorkerPool* workers_ = nullptr;
    std::mutex mutex_;
    std::condition_variable posted_;
    std::deque<UniqueFunction<void()>> ready_;
    std::multimap<SteadyTime, UniqueFunction<void()>> timed_;
};

inline constexpr MessageId kDiskError{"engine_test.disk_error"};
inline constexpr MessageId kDiskLocked{"engine_test.disk_locked"};

// IFileSystem over a real scratch directory: the downloader, the component store and the library
// walk work on real files, so a composed engine needs them where the port writes too.
class DiskFileSystem final : public ports::IFileSystem {
public:
    Result<void> atomic_replace(const NativePath& target, std::span<const u8> bytes, bool keep_backup) override {
        std::error_code error;
        std::filesystem::create_directories(target.parent_path(), error);
        NativePath temp = target;
        temp += ".tmp";
        {
            std::ofstream out(temp, std::ios::binary | std::ios::trunc);
            if (!out) return fail(temp, ErrorKind::Generic);
            out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
            if (!out) return fail(temp, ErrorKind::Generic);
        }
        if (keep_backup && std::filesystem::exists(target, error)) {
            NativePath backup = target;
            backup += ".bak";
            std::filesystem::copy_file(target, backup, std::filesystem::copy_options::overwrite_existing, error);
        }
        std::filesystem::rename(temp, target, error);
        if (error) return fail(target, ErrorKind::Generic);
        return {};
    }

    Result<std::vector<u8>> read_all(const NativePath& path) override {
        std::error_code error;
        if (!std::filesystem::is_regular_file(path, error)) return fail(path, ErrorKind::NotFound);
        std::ifstream in(path, std::ios::binary);
        return std::vector<u8>(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    }

    // One holder per path in this process, which is all a test engine needs.
    Result<ports::FileLock> lock_exclusive(const NativePath& path, bool) override {
        std::error_code error;
        std::filesystem::create_directories(path.parent_path(), error);
        const std::string key = path.lexically_normal().generic_string();
        const std::scoped_lock lock(state_->mutex);
        if (!state_->locked.insert(key).second)
            return std::unexpected(make_diag(ErrorDomain::Internal, kDiskLocked).arg("path", path).kind(ErrorKind::Conflict).build());
        return ports::FileLock(std::make_unique<LockHandle>(state_, key));
    }

    Result<void> restrict_to_owner(const NativePath&) override { return {}; }

    Result<ports::HeldFile> open_deny_write(const NativePath& path) override {
        std::error_code error;
        if (!std::filesystem::is_regular_file(path, error)) return fail(path, ErrorKind::NotFound);
        auto handle = std::make_unique<StreamHandle>(path);
        if (!handle->in) return fail(path, ErrorKind::Generic);
        return ports::HeldFile(std::move(handle));
    }

    Result<ports::FileRevision> revision(const NativePath& path) override {
        std::error_code error;
        const auto status = std::filesystem::status(path, error);
        if (error || !std::filesystem::exists(status)) return fail(path, ErrorKind::NotFound);
        ports::FileRevision revision;
        if (std::filesystem::is_regular_file(status)) revision.size = std::filesystem::file_size(path, error);
        revision.file_id = std::hash<std::string>{}(path.lexically_normal().generic_string());
        return revision;
    }

    Result<ports::SharedRead> read_shared(const NativePath& path, u64 offset, std::size_t max_bytes) override {
        auto all = read_all(path);
        if (!all) return std::unexpected(std::move(all.error()));
        ports::SharedRead out;
        out.revision.size = all->size();
        if (offset < all->size()) {
            const std::size_t start = static_cast<std::size_t>(offset);
            const std::size_t count = (std::min)(max_bytes, all->size() - start);
            out.bytes.assign(all->begin() + static_cast<std::ptrdiff_t>(start),
                             all->begin() + static_cast<std::ptrdiff_t>(start + count));
        }
        return out;
    }

    Result<void> create_dirs_owner_only(const NativePath& path) override {
        std::error_code error;
        std::filesystem::create_directories(path, error);
        if (error) return fail(path, ErrorKind::Generic);
        return {};
    }

    Result<void> remove_tree(const NativePath& path) override {
        std::error_code error;
        std::filesystem::remove_all(path, error);
        if (error) return fail(path, ErrorKind::Generic);
        return {};
    }

private:
    struct LockState {
        std::mutex mutex;
        std::set<std::string> locked;
    };

    struct LockHandle final : ports::FileLock::Handle {
        LockHandle(std::shared_ptr<LockState> held_state, std::string held_key)
            : state(std::move(held_state)), key(std::move(held_key)) {}
        ~LockHandle() override {
            const std::scoped_lock lock(state->mutex);
            state->locked.erase(key);
        }
        std::shared_ptr<LockState> state;
        std::string key;
    };

    struct StreamHandle final : ports::HeldFile::Handle {
        explicit StreamHandle(const NativePath& path) : in(path, std::ios::binary) {}

        Result<std::size_t> read(std::span<u8> out) override {
            in.read(reinterpret_cast<char*>(out.data()), static_cast<std::streamsize>(out.size()));
            return static_cast<std::size_t>(in.gcount());
        }

        std::ifstream in;
    };

    [[nodiscard]] static std::unexpected<Diagnostic> fail(const NativePath& path, ErrorKind kind) {
        return std::unexpected(make_diag(ErrorDomain::Internal, kDiskError).arg("path", path).kind(kind).build());
    }

    std::shared_ptr<LockState> state_ = std::make_shared<LockState>();
};

inline void write_file(const NativePath& path, std::string_view text) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out.write(text.data(), static_cast<std::streamsize>(text.size()));
}

[[nodiscard]] inline std::string read_file(const NativePath& path) {
    std::ifstream in(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

[[nodiscard]] inline std::string describe(const Diagnostic& diag) {
    std::string out = diag.id;
    for (const Diagnostic& cause : diag.causes) out += " <- " + describe(cause);
    return out;
}

}  // namespace rb::engine::test

// A failed REQUIRE on a Result names the diagnostic chain instead of {?}.
template <class T>
struct Catch::StringMaker<std::expected<T, rb::Diagnostic>> {
    static std::string convert(const std::expected<T, rb::Diagnostic>& result) {
        return result ? std::string("ok") : "error " + rb::engine::test::describe(result.error());
    }
};
