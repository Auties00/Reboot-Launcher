#include "reboot/testing/fake_file_watcher.hpp"

#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <utility>
#include <vector>

#include "reboot/foundation/paths.hpp"
#include "reboot/testing/in_memory_file_system.hpp"

namespace reboot::testing {
namespace {

using Callback = UniqueFunction<void(ports::FileChange)>;

struct WatcherState {
    explicit WatcherState(Executor& executor) : deliver_on(executor) {}

    struct Watch {
        NativePath dir;
        // Shared with a delivery in progress, which may destroy its own handle.
        std::shared_ptr<Callback> callback;
    };

    mutable std::mutex mutex;
    Executor& deliver_on;
    std::map<u64, Watch> watches;
    u64 next_id = 1;
    bool overflowing = false;
    std::optional<Diagnostic> next_error;
};

void emit_to(const std::shared_ptr<WatcherState>& state, const ports::FileChange& change) {
    std::vector<u64> targets;
    {
        const std::scoped_lock lock(state->mutex);
        if (state->overflowing) return;
        for (const auto& [id, watch] : state->watches)
            if (is_inside(change.path, watch.dir) && change.path.lexically_normal() != watch.dir.lexically_normal())
                targets.push_back(id);
    }
    for (const u64 id : targets) {
        state->deliver_on.post([state, id, change] {
            std::shared_ptr<Callback> callback;
            {
                const std::scoped_lock lock(state->mutex);
                const auto it = state->watches.find(id);
                if (it == state->watches.end()) return;
                callback = it->second.callback;
            }
            if (*callback) (*callback)(change);
        });
    }
}

class FakeWatch final : public ports::WatchHandle::Handle {
public:
    FakeWatch(std::shared_ptr<WatcherState> state, u64 id) : state_(std::move(state)), id_(id) {}
    ~FakeWatch() override {
        std::shared_ptr<Callback> dropped;
        const std::scoped_lock lock(state_->mutex);
        if (const auto it = state_->watches.find(id_); it != state_->watches.end()) {
            dropped = std::move(it->second.callback);
            state_->watches.erase(it);
        }
    }
    FakeWatch(const FakeWatch&) = delete;
    FakeWatch& operator=(const FakeWatch&) = delete;

private:
    std::shared_ptr<WatcherState> state_;
    u64 id_;
};

}  // namespace

struct FakeFileWatcher::State : WatcherState {
    using WatcherState::WatcherState;
};

FakeFileWatcher::FakeFileWatcher(Executor& deliver_on) : state_(std::make_shared<State>(deliver_on)) {}

FakeFileWatcher::~FakeFileWatcher() = default;

Result<ports::WatchHandle> FakeFileWatcher::watch(const NativePath& dir, UniqueFunction<void(ports::FileChange)> on_change) {
    const std::scoped_lock lock(state_->mutex);
    if (state_->next_error) {
        Diagnostic error = std::move(*state_->next_error);
        state_->next_error.reset();
        return std::unexpected(std::move(error));
    }
    const u64 id = state_->next_id++;
    state_->watches.emplace(id, WatcherState::Watch{dir, std::make_shared<Callback>(std::move(on_change))});
    return ports::WatchHandle(std::make_unique<FakeWatch>(state_, id));
}

void FakeFileWatcher::emit(ports::FileChange change) { emit_to(state_, change); }

void FakeFileWatcher::follow(InMemoryFileSystem& fs) {
    const std::weak_ptr<WatcherState> weak = std::shared_ptr<WatcherState>(state_);
    fs.set_change_listener([weak](const ports::FileChange& change) {
        if (const auto state = weak.lock()) emit_to(state, change);
    });
}

void FakeFileWatcher::set_overflowing(bool overflowing) {
    const std::scoped_lock lock(state_->mutex);
    state_->overflowing = overflowing;
}

void FakeFileWatcher::fail_next_watch(Diagnostic error) {
    const std::scoped_lock lock(state_->mutex);
    state_->next_error = std::move(error);
}

std::size_t FakeFileWatcher::live_watches() const {
    const std::scoped_lock lock(state_->mutex);
    return state_->watches.size();
}

}  // namespace reboot::testing
