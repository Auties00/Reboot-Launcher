#include "reboot/components/deletion_guard.hpp"

#include <map>
#include <set>
#include <utility>

#include "reboot/foundation/executor.hpp"
#include "reboot/ports/file_system.hpp"

namespace rb::components {

struct DeletionGuard::Impl {
    struct DirWatch {
        u64 id = 0;
        ports::WatchHandle handle;
        std::set<NativePath> files;
    };

    Impl(ports::IFileWatcher& file_watcher, Executor& strand_in, OnChanged changed)
        : watcher(file_watcher), strand(strand_in), on_changed(std::move(changed)) {}

    void deliver(u64 watch_id, const ports::FileChange& change) {
        if (change.kind == ports::FileChangeKind::Created) return;
        const NativePath file = change.path.lexically_normal();
        const auto dir = dirs.find(file.parent_path());
        // A change posted before its watch ended is dropped, even if the directory is watched again.
        if (dir == dirs.end() || dir->second.id != watch_id) return;
        if (dir->second.files.contains(file)) on_changed(file);
    }

    ports::IFileWatcher& watcher;
    Executor& strand;
    OnChanged on_changed;
    std::map<NativePath, DirWatch> dirs;
    u64 next_id = 1;
};

DeletionGuard::DeletionGuard(ports::IFileWatcher& watcher, Executor& strand, OnChanged on_changed)
    : impl_(std::make_unique<Impl>(watcher, strand, std::move(on_changed))) {}

DeletionGuard::~DeletionGuard() = default;

Result<void> DeletionGuard::track(const NativePath& file) {
    const NativePath normal = file.lexically_normal();
    const NativePath dir = normal.parent_path();
    if (const auto found = impl_->dirs.find(dir); found != impl_->dirs.end()) {
        found->second.files.insert(normal);
        return {};
    }
    const u64 id = impl_->next_id++;
    Impl* impl = impl_.get();
    auto handle = impl_->watcher.watch(dir, [impl, id](ports::FileChange change) {
        impl->strand.post([impl, id, change = std::move(change)] { impl->deliver(id, change); });
    });
    if (!handle) return std::unexpected(std::move(handle.error()));
    Impl::DirWatch watch{.id = id, .handle = std::move(*handle), .files = {normal}};
    impl_->dirs.emplace(dir, std::move(watch));
    return {};
}

void DeletionGuard::untrack(const NativePath& file) {
    const NativePath normal = file.lexically_normal();
    const auto dir = impl_->dirs.find(normal.parent_path());
    if (dir == impl_->dirs.end()) return;
    dir->second.files.erase(normal);
    if (dir->second.files.empty()) impl_->dirs.erase(dir);
}

}  // namespace rb::components
