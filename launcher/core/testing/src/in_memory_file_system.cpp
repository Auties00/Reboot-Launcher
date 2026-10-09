#include "reboot/testing/in_memory_file_system.hpp"

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <iterator>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "messages.hpp"
#include "reboot/foundation/paths.hpp"

namespace reboot::testing {
namespace {

// Symbolic links followed on one path before it counts as a loop.
constexpr int kMaxLinkHops = 16;

enum class NodeKind : u8 { File, Dir, Link };

struct Node {
    NodeKind kind = NodeKind::File;
    std::vector<u8> bytes;
    NativePath link_target;
    bool owner_only = false;
    std::chrono::system_clock::time_point mtime;
    u64 file_id = 0;
};

using Changes = std::vector<ports::FileChange>;
using Listener = UniqueFunction<void(const ports::FileChange&)>;

// Lexically normal, without the empty element a trailing separator leaves.
[[nodiscard]] NativePath normal(const NativePath& path) {
    NativePath out = path.lexically_normal();
    if (!out.has_filename() && out.has_relative_path()) out = out.parent_path();
    return out;
}

[[nodiscard]] bool is_root(const NativePath& path) { return path == path.root_path(); }

[[nodiscard]] NativePath with_suffix(const NativePath& path, std::string_view suffix) {
    NativePath out = path;
    out += NativePath(suffix);
    return out;
}

[[nodiscard]] Diagnostic not_found(const NativePath& path) {
    return make_diag(kTestingDomain, msg::kNotFound).arg("path", path).kind(ErrorKind::NotFound);
}

[[nodiscard]] Diagnostic not_a_directory(const NativePath& path) {
    return make_diag(kTestingDomain, msg::kNotADirectory).arg("path", path).kind(ErrorKind::Conflict);
}

[[nodiscard]] Diagnostic is_a_directory(const NativePath& path) {
    return make_diag(kTestingDomain, msg::kIsADirectory).arg("path", path).kind(ErrorKind::Conflict);
}

[[nodiscard]] Diagnostic held_open(const NativePath& path) {
    return make_diag(kTestingDomain, msg::kHeldOpen).arg("path", path).kind(ErrorKind::Conflict);
}

// Shared with lock and held-file handles, which may outlive the file system.
struct FsState {
    mutable std::mutex mutex;
    std::condition_variable lock_released;
    std::map<NativePath, Node> nodes;
    std::map<NativePath, u64> locks;
    std::map<NativePath, std::size_t> held;
    std::vector<NativePath> crash_next;
    std::chrono::milliseconds lock_wait_limit{0};
    const IClock* clock = nullptr;
    std::chrono::system_clock::time_point counter_time{std::chrono::hours{24 * 365 * 30}};
    u64 next_file_id = 1;
    u64 next_lock_id = 1;
    u64 next_temp = 1;
    std::shared_ptr<Listener> listener;

    std::chrono::system_clock::time_point now() {
        if (clock != nullptr) return clock->system_now();
        counter_time += std::chrono::seconds{1};
        return counter_time;
    }

    [[nodiscard]] Node* find(const NativePath& path) {
        const auto it = nodes.find(path);
        return it == nodes.end() ? nullptr : &it->second;
    }

    [[nodiscard]] bool dir_exists(const NativePath& path) {
        if (is_root(path)) return true;
        const Node* node = find(path);
        return node != nullptr && node->kind == NodeKind::Dir;
    }

    // Follows links in every component, and in the last one too when `follow_last`.
    [[nodiscard]] Result<NativePath> resolve(const NativePath& path, bool follow_last) {
        NativePath current = normal(path);
        for (int hops = 0; hops <= kMaxLinkHops; ++hops) {
            NativePath walked = current.root_path();
            bool replaced = false;
            const NativePath relative = current.relative_path();
            for (auto it = relative.begin(); it != relative.end(); ++it) {
                walked /= *it;
                const Node* node = find(walked);
                if (node == nullptr || node->kind != NodeKind::Link) continue;
                if (std::next(it) == relative.end() && !follow_last) break;
                NativePath target =
                    node->link_target.is_absolute() ? node->link_target : walked.parent_path() / node->link_target;
                for (auto rest = std::next(it); rest != relative.end(); ++rest) target /= *rest;
                current = normal(target);
                replaced = true;
                break;
            }
            if (!replaced) return current;
        }
        return make_diag(kTestingDomain, msg::kIsALink).arg("path", path).kind(ErrorKind::Conflict).fail();
    }

    [[nodiscard]] Result<void> require_parent(const NativePath& path) {
        const NativePath parent = path.parent_path();
        if (parent.empty() || dir_exists(parent)) return {};
        if (find(parent) != nullptr) return std::unexpected(not_a_directory(parent));
        return std::unexpected(not_found(parent));
    }

    // Creates every missing directory of `path`; existing ones must be directories.
    [[nodiscard]] Result<void> make_dirs(const NativePath& path, bool owner_only, Changes& changes) {
        NativePath walked = path.root_path();
        for (const NativePath& part : path.relative_path()) {
            walked /= part;
            if (const Node* node = find(walked); node != nullptr) {
                if (node->kind != NodeKind::Dir) return std::unexpected(not_a_directory(walked));
                continue;
            }
            Node dir;
            dir.kind = NodeKind::Dir;
            dir.owner_only = owner_only;
            dir.mtime = now();
            dir.file_id = next_file_id++;
            nodes.emplace(walked, std::move(dir));
            changes.push_back({walked, ports::FileChangeKind::Created});
        }
        return {};
    }

    void put_file(const NativePath& path, std::span<const u8> bytes, Changes& changes) {
        Node file;
        file.bytes.assign(bytes.begin(), bytes.end());
        file.mtime = now();
        // A new id per write, as the rename of a fresh temporary file gives a new inode.
        file.file_id = next_file_id++;
        const bool existed = nodes.contains(path);
        nodes.insert_or_assign(path, std::move(file));
        changes.push_back({path, existed ? ports::FileChangeKind::Modified : ports::FileChangeKind::Created});
    }

    [[nodiscard]] bool held_under(const NativePath& path) const {
        return std::ranges::any_of(held, [&](const auto& entry) { return is_inside(entry.first, path); });
    }

    // Outside the lock, so a listener may call back into the file system.
    void notify(const Changes& changes) {
        if (changes.empty()) return;
        std::shared_ptr<Listener> current;
        {
            const std::scoped_lock lock(mutex);
            current = listener;
        }
        if (!current || !*current) return;
        for (const ports::FileChange& change : changes) (*current)(change);
    }
};

class MemoryLock final : public ports::FileLock::Handle {
public:
    MemoryLock(std::shared_ptr<FsState> state, NativePath path, u64 id)
        : state_(std::move(state)), path_(std::move(path)), id_(id) {}
    ~MemoryLock() override {
        {
            const std::scoped_lock lock(state_->mutex);
            if (const auto it = state_->locks.find(path_); it != state_->locks.end() && it->second == id_)
                state_->locks.erase(it);
        }
        state_->lock_released.notify_all();
    }
    MemoryLock(const MemoryLock&) = delete;
    MemoryLock& operator=(const MemoryLock&) = delete;

private:
    std::shared_ptr<FsState> state_;
    NativePath path_;
    u64 id_;
};

// Reads the bytes the file held when it was opened, as a deny-write handle guarantees.
class MemoryHeldFile final : public ports::HeldFile::Handle {
public:
    MemoryHeldFile(std::shared_ptr<FsState> state, NativePath path, std::vector<u8> bytes)
        : state_(std::move(state)), path_(std::move(path)), bytes_(std::move(bytes)) {}
    ~MemoryHeldFile() override {
        const std::scoped_lock lock(state_->mutex);
        if (const auto it = state_->held.find(path_); it != state_->held.end() && --it->second == 0)
            state_->held.erase(it);
    }
    MemoryHeldFile(const MemoryHeldFile&) = delete;
    MemoryHeldFile& operator=(const MemoryHeldFile&) = delete;

    Result<std::size_t> read(std::span<u8> out) override {
        const std::size_t n = std::min(out.size(), bytes_.size() - offset_);
        std::copy_n(bytes_.begin() + static_cast<std::ptrdiff_t>(offset_), n, out.begin());
        offset_ += n;
        return n;
    }

private:
    std::shared_ptr<FsState> state_;
    NativePath path_;
    std::vector<u8> bytes_;
    std::size_t offset_ = 0;
};

}  // namespace

struct InMemoryFileSystem::Impl {
    std::shared_ptr<FsState> state = std::make_shared<FsState>();
};

InMemoryFileSystem::InMemoryFileSystem() : impl_(std::make_unique<Impl>()) {}

InMemoryFileSystem::InMemoryFileSystem(const IClock& clock) : impl_(std::make_unique<Impl>()) {
    impl_->state->clock = &clock;
}

InMemoryFileSystem::~InMemoryFileSystem() = default;

Result<void> InMemoryFileSystem::atomic_replace(const NativePath& target, std::span<const u8> bytes, bool keep_backup) {
    if (auto error = faults_.take(FsOperation::AtomicReplace)) return std::unexpected(std::move(*error));
    Changes changes;
    const Result<void> result = [&]() -> Result<void> {
        const std::scoped_lock lock(impl_->state->mutex);
        auto resolved = impl_->state->resolve(target, false);
        if (!resolved) return std::unexpected(std::move(resolved.error()));
        const NativePath& path = *resolved;
        if (auto parent = impl_->state->require_parent(path); !parent) return parent;
        const Node* existing = impl_->state->find(path);
        if (existing != nullptr && existing->kind == NodeKind::Dir) return std::unexpected(is_a_directory(path));
        if (impl_->state->held.contains(path)) return std::unexpected(held_open(path));

        if (const auto crash = std::ranges::find(impl_->state->crash_next, path); crash != impl_->state->crash_next.end()) {
            impl_->state->crash_next.erase(crash);
            impl_->state->put_file(with_suffix(path, ".tmp" + std::to_string(impl_->state->next_temp++)), bytes, changes);
            return make_diag(kTestingDomain, msg::kSimulatedCrash).arg("path", path).fail();
        }
        if (keep_backup && existing != nullptr && existing->kind == NodeKind::File) {
            const NativePath backup = with_suffix(path, ".bak");
            const bool had_backup = impl_->state->nodes.contains(backup);
            // The backup is the previous file itself, as a hard link keeps it.
            Node previous = *existing;
            impl_->state->nodes.insert_or_assign(backup, std::move(previous));
            changes.push_back({backup, had_backup ? ports::FileChangeKind::Modified : ports::FileChangeKind::Created});
        }
        impl_->state->put_file(path, bytes, changes);
        return {};
    }();
    impl_->state->notify(changes);
    return result;
}

Result<std::vector<u8>> InMemoryFileSystem::read_all(const NativePath& path) {
    if (auto error = faults_.take(FsOperation::ReadAll)) return std::unexpected(std::move(*error));
    const std::scoped_lock lock(impl_->state->mutex);
    auto resolved = impl_->state->resolve(path, true);
    if (!resolved) return std::unexpected(std::move(resolved.error()));
    const Node* node = impl_->state->find(*resolved);
    if (node == nullptr) return std::unexpected(not_found(path));
    if (node->kind == NodeKind::Dir) return std::unexpected(is_a_directory(path));
    return node->bytes;
}

Result<ports::FileLock> InMemoryFileSystem::lock_exclusive(const NativePath& path, bool wait) {
    if (auto error = faults_.take(FsOperation::LockExclusive)) return std::unexpected(std::move(*error));
    Changes changes;
    u64 id = 0;
    NativePath locked_path;
    {
        std::unique_lock lock(impl_->state->mutex);
        auto resolved = impl_->state->resolve(path, true);
        if (!resolved) return std::unexpected(std::move(resolved.error()));
        locked_path = *resolved;
        if (auto parent = impl_->state->require_parent(locked_path); !parent) return std::unexpected(std::move(parent.error()));
        const Node* node = impl_->state->find(locked_path);
        if (node != nullptr && node->kind == NodeKind::Dir) return std::unexpected(is_a_directory(path));
        // The lock file is created, as opening it for locking does.
        if (node == nullptr) impl_->state->put_file(locked_path, {}, changes);

        if (impl_->state->locks.contains(locked_path)) {
            if (!wait)
                return make_diag(kTestingDomain, msg::kLockHeld).arg("path", path).kind(ErrorKind::Conflict).fail();
            const auto limit = impl_->state->lock_wait_limit;
            const bool freed = impl_->state->lock_released.wait_for(lock, limit, [&] { return !impl_->state->locks.contains(locked_path); });
            if (!freed)
                return make_diag(kTestingDomain, msg::kLockWaitExceeded)
                    .arg("path", path)
                    .arg("limit", limit)
                    .kind(ErrorKind::Conflict)
                    .fail();
        }
        id = impl_->state->next_lock_id++;
        impl_->state->locks.emplace(locked_path, id);
    }
    impl_->state->notify(changes);
    return ports::FileLock(std::make_unique<MemoryLock>(impl_->state, std::move(locked_path), id));
}

Result<void> InMemoryFileSystem::restrict_to_owner(const NativePath& path) {
    if (auto error = faults_.take(FsOperation::RestrictToOwner)) return std::unexpected(std::move(*error));
    const std::scoped_lock lock(impl_->state->mutex);
    auto resolved = impl_->state->resolve(path, false);
    if (!resolved) return std::unexpected(std::move(resolved.error()));
    Node* node = impl_->state->find(*resolved);
    if (node == nullptr) return std::unexpected(not_found(path));
    if (node->kind == NodeKind::Link)
        return make_diag(kTestingDomain, msg::kIsALink).arg("path", path).kind(ErrorKind::Conflict).fail();
    node->owner_only = true;
    return {};
}

Result<ports::HeldFile> InMemoryFileSystem::open_deny_write(const NativePath& path) {
    if (auto error = faults_.take(FsOperation::OpenDenyWrite)) return std::unexpected(std::move(*error));
    const std::scoped_lock lock(impl_->state->mutex);
    auto resolved = impl_->state->resolve(path, true);
    if (!resolved) return std::unexpected(std::move(resolved.error()));
    const Node* node = impl_->state->find(*resolved);
    if (node == nullptr) return std::unexpected(not_found(path));
    if (node->kind == NodeKind::Dir) return std::unexpected(is_a_directory(path));
    ++impl_->state->held[*resolved];
    return ports::HeldFile(std::make_unique<MemoryHeldFile>(impl_->state, *resolved, node->bytes));
}

Result<ports::FileRevision> InMemoryFileSystem::revision(const NativePath& path) {
    if (auto error = faults_.take(FsOperation::Revision)) return std::unexpected(std::move(*error));
    const std::scoped_lock lock(impl_->state->mutex);
    auto resolved = impl_->state->resolve(path, true);
    if (!resolved) return std::unexpected(std::move(resolved.error()));
    if (is_root(*resolved)) return ports::FileRevision{};
    const Node* node = impl_->state->find(*resolved);
    if (node == nullptr) return std::unexpected(not_found(path));
    return ports::FileRevision{node->bytes.size(), node->mtime, node->file_id};
}

Result<ports::SharedRead> InMemoryFileSystem::read_shared(const NativePath& path, u64 offset, std::size_t max_bytes) {
    if (auto error = faults_.take(FsOperation::ReadShared)) return std::unexpected(std::move(*error));
    const std::scoped_lock lock(impl_->state->mutex);
    auto resolved = impl_->state->resolve(path, true);
    if (!resolved) return std::unexpected(std::move(resolved.error()));
    const Node* node = impl_->state->find(*resolved);
    if (node == nullptr) return std::unexpected(not_found(path));
    if (node->kind == NodeKind::Dir) return std::unexpected(is_a_directory(path));
    ports::SharedRead read;
    read.revision = ports::FileRevision{node->bytes.size(), node->mtime, node->file_id};
    if (offset < node->bytes.size()) {
        const auto begin = node->bytes.begin() + static_cast<std::ptrdiff_t>(offset);
        const std::size_t count = std::min<std::size_t>(max_bytes, node->bytes.size() - static_cast<std::size_t>(offset));
        read.bytes.assign(begin, begin + static_cast<std::ptrdiff_t>(count));
    }
    return read;
}

Result<void> InMemoryFileSystem::create_dirs_owner_only(const NativePath& path) {
    if (auto error = faults_.take(FsOperation::CreateDirsOwnerOnly)) return std::unexpected(std::move(*error));
    Changes changes;
    const Result<void> result = [&]() -> Result<void> {
        const std::scoped_lock lock(impl_->state->mutex);
        auto resolved = impl_->state->resolve(path, true);
        if (!resolved) return std::unexpected(std::move(resolved.error()));
        return impl_->state->make_dirs(*resolved, true, changes);
    }();
    impl_->state->notify(changes);
    return result;
}

Result<void> InMemoryFileSystem::remove_tree(const NativePath& path) {
    if (auto error = faults_.take(FsOperation::RemoveTree)) return std::unexpected(std::move(*error));
    Changes changes;
    const Result<void> result = [&]() -> Result<void> {
        const std::scoped_lock lock(impl_->state->mutex);
        auto resolved = impl_->state->resolve(path, false);
        if (!resolved) return std::unexpected(std::move(resolved.error()));
        const NativePath& top = *resolved;
        const Node* node = impl_->state->find(top);
        if (node == nullptr) return {};
        if (impl_->state->held_under(top)) return std::unexpected(held_open(top));
        std::vector<NativePath> doomed{top};
        // A link is removed itself; what it points at stays.
        if (node->kind == NodeKind::Dir)
            for (const auto& [key, value] : impl_->state->nodes)
                if (key != top && is_inside(key, top)) doomed.push_back(key);
        // Deepest first, as a recursive delete reports them.
        std::ranges::sort(doomed, std::greater<>{});
        for (const NativePath& gone : doomed) {
            impl_->state->nodes.erase(gone);
            changes.push_back({gone, ports::FileChangeKind::Removed});
        }
        return {};
    }();
    impl_->state->notify(changes);
    return result;
}

void InMemoryFileSystem::write(const NativePath& path, std::span<const u8> bytes) {
    Changes changes;
    {
        const std::scoped_lock lock(impl_->state->mutex);
        auto resolved = impl_->state->resolve(path, true);
        if (!resolved) return;
        if (!impl_->state->make_dirs(resolved->parent_path(), false, changes)) return;
        impl_->state->put_file(*resolved, bytes, changes);
    }
    impl_->state->notify(changes);
}

void InMemoryFileSystem::append(const NativePath& path, std::span<const u8> bytes) {
    Changes changes;
    {
        const std::scoped_lock lock(impl_->state->mutex);
        auto resolved = impl_->state->resolve(path, true);
        if (!resolved) return;
        if (!impl_->state->make_dirs(resolved->parent_path(), false, changes)) return;
        Node* node = impl_->state->find(*resolved);
        if (node == nullptr || node->kind != NodeKind::File) {
            impl_->state->put_file(*resolved, bytes, changes);
        } else {
            node->bytes.insert(node->bytes.end(), bytes.begin(), bytes.end());
            node->mtime = impl_->state->now();
            changes.push_back({*resolved, ports::FileChangeKind::Modified});
        }
    }
    impl_->state->notify(changes);
}

void InMemoryFileSystem::write_text(const NativePath& path, std::string_view text) {
    write(path, std::span(reinterpret_cast<const u8*>(text.data()), text.size()));
}

void InMemoryFileSystem::make_dir(const NativePath& path) {
    Changes changes;
    {
        const std::scoped_lock lock(impl_->state->mutex);
        if (auto resolved = impl_->state->resolve(path, true)) (void)impl_->state->make_dirs(*resolved, false, changes);
    }
    impl_->state->notify(changes);
}

void InMemoryFileSystem::make_symlink(const NativePath& link, const NativePath& target) {
    Changes changes;
    {
        const std::scoped_lock lock(impl_->state->mutex);
        auto resolved = impl_->state->resolve(link, false);
        if (!resolved || !impl_->state->make_dirs(resolved->parent_path(), false, changes)) return;
        Node node;
        node.kind = NodeKind::Link;
        node.link_target = target;
        node.mtime = impl_->state->now();
        node.file_id = impl_->state->next_file_id++;
        const bool existed = impl_->state->nodes.contains(*resolved);
        impl_->state->nodes.insert_or_assign(*resolved, std::move(node));
        changes.push_back({*resolved, existed ? ports::FileChangeKind::Modified : ports::FileChangeKind::Created});
    }
    impl_->state->notify(changes);
}

std::optional<std::vector<u8>> InMemoryFileSystem::contents(const NativePath& path) const {
    const std::scoped_lock lock(impl_->state->mutex);
    auto resolved = impl_->state->resolve(path, true);
    if (!resolved) return std::nullopt;
    const Node* node = impl_->state->find(*resolved);
    if (node == nullptr || node->kind != NodeKind::File) return std::nullopt;
    return node->bytes;
}

std::optional<std::string> InMemoryFileSystem::text(const NativePath& path) const {
    auto bytes = contents(path);
    if (!bytes) return std::nullopt;
    return std::string(bytes->begin(), bytes->end());
}

bool InMemoryFileSystem::exists(const NativePath& path) const {
    const std::scoped_lock lock(impl_->state->mutex);
    auto resolved = impl_->state->resolve(path, false);
    return resolved && (is_root(*resolved) || impl_->state->find(*resolved) != nullptr);
}

bool InMemoryFileSystem::is_dir(const NativePath& path) const {
    const std::scoped_lock lock(impl_->state->mutex);
    auto resolved = impl_->state->resolve(path, true);
    return resolved && impl_->state->dir_exists(*resolved);
}

bool InMemoryFileSystem::owner_only(const NativePath& path) const {
    const std::scoped_lock lock(impl_->state->mutex);
    auto resolved = impl_->state->resolve(path, false);
    if (!resolved) return false;
    const Node* node = impl_->state->find(*resolved);
    return node != nullptr && node->owner_only;
}

bool InMemoryFileSystem::locked(const NativePath& path) const {
    const std::scoped_lock lock(impl_->state->mutex);
    auto resolved = impl_->state->resolve(path, true);
    return resolved && impl_->state->locks.contains(*resolved);
}

std::vector<NativePath> InMemoryFileSystem::list(const NativePath& dir) const {
    const std::scoped_lock lock(impl_->state->mutex);
    std::vector<NativePath> out;
    auto resolved = impl_->state->resolve(dir, true);
    if (!resolved) return out;
    for (const auto& [key, node] : impl_->state->nodes)
        if (key.parent_path() == *resolved && key != *resolved) out.push_back(key);
    return out;
}

void InMemoryFileSystem::set_lock_wait_limit(std::chrono::milliseconds limit) {
    const std::scoped_lock lock(impl_->state->mutex);
    impl_->state->lock_wait_limit = limit;
}

void InMemoryFileSystem::crash_during_next_replace(const NativePath& target) {
    const std::scoped_lock lock(impl_->state->mutex);
    if (auto resolved = impl_->state->resolve(target, false)) impl_->state->crash_next.push_back(*resolved);
}

void InMemoryFileSystem::set_change_listener(UniqueFunction<void(const ports::FileChange&)> listener) {
    auto shared = std::make_shared<Listener>(std::move(listener));
    const std::scoped_lock lock(impl_->state->mutex);
    impl_->state->listener = std::move(shared);
}

}  // namespace reboot::testing
