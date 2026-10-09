#include "darwin.hpp"

#include "reboot/os_macos/platform/fs_events_watcher.hpp"

#include <CoreServices/CoreServices.h>
#include <dispatch/dispatch.h>
#include <sys/stat.h>

#include <cerrno>
#include <cstdlib>
#include <memory>
#include <string>
#include <utility>

#include "cf_ptr.hpp"
#include "fs_event_change.hpp"
#include "messages.hpp"
#include "reboot/foundation/log.hpp"
#include "reboot/posix/posix_error.hpp"

// The kFSEventStream constants are C-style casts in Apple's headers.
#pragma clang diagnostic ignored "-Wold-style-cast"

namespace rb::os_macos::platform {

namespace {

// Short enough for a settings file written by hand to show up promptly.
constexpr CFTimeInterval kLatencySeconds = 0.05;

[[nodiscard]] FsEventFlags flags_of(FSEventStreamEventFlags raw) noexcept {
    FsEventFlags flags;
    flags.dropped = (raw & (kFSEventStreamEventFlagMustScanSubDirs | kFSEventStreamEventFlagUserDropped |
                            kFSEventStreamEventFlagKernelDropped)) != 0;
    flags.created = (raw & kFSEventStreamEventFlagItemCreated) != 0;
    flags.removed = (raw & kFSEventStreamEventFlagItemRemoved) != 0;
    flags.renamed = (raw & kFSEventStreamEventFlagItemRenamed) != 0;
    flags.modified = (raw & (kFSEventStreamEventFlagItemModified | kFSEventStreamEventFlagItemInodeMetaMod |
                             kFSEventStreamEventFlagItemFinderInfoMod | kFSEventStreamEventFlagItemChangeOwner |
                             kFSEventStreamEventFlagItemXattrMod)) != 0;
    return flags;
}

// What the stream's callback reaches. Callbacks run one at a time on the watcher's serial queue.
struct Watch {
    NativePath dir;
    NativePath real_dir;
    UniqueFunction<void(ports::FileChange)> on_change;
    bool stopped = false;
};

void on_events(ConstFSEventStreamRef, void* info, std::size_t count, void* paths, const FSEventStreamEventFlags flags[],
               const FSEventStreamEventId[]) {
    auto& watch = *static_cast<Watch*>(info);
    auto* const* event_paths = static_cast<char* const*>(paths);
    try {
        for (std::size_t i = 0; i < count && !watch.stopped; ++i) {
            struct stat entry {};
            const bool exists = ::lstat(event_paths[i], &entry) == 0;
            std::optional<ports::FileChange> change =
                fs_event_change(watch.dir, watch.real_dir, event_paths[i], flags_of(flags[i]), exists);
            if (change) watch.on_change(std::move(*change));
        }
    } catch (...) {
        REBOOT_LOG_ERROR(Engine, "internal.bug: a file watcher callback threw");
    }
}

void stop_stream(void* stream) {
    ::FSEventStreamStop(static_cast<FSEventStreamRef>(stream));
    ::FSEventStreamInvalidate(static_cast<FSEventStreamRef>(stream));
}

void delete_watch(void* watch) { delete static_cast<Watch*>(watch); }

void release_stream(void* stream) { ::FSEventStreamRelease(static_cast<FSEventStreamRef>(stream)); }

const char kQueueKey = 0;

// Stopping on the stream's own queue means no callback runs once the handle is gone.
class FsEventsHandle final : public ports::WatchHandle::Handle {
public:
    FsEventsHandle(FSEventStreamRef stream, dispatch_queue_t queue, std::unique_ptr<Watch> watch)
        : stream_(stream), queue_(queue), watch_(std::move(watch)) {
        ::dispatch_retain(queue_);
    }

    ~FsEventsHandle() override {
        if (::dispatch_get_specific(&kQueueKey) == queue_) {
            // Destroyed from inside a callback: the stream and the watch must outlive that call.
            watch_->stopped = true;
            stop_stream(stream_);
            ::dispatch_async_f(queue_, stream_, &release_stream);
            ::dispatch_async_f(queue_, watch_.release(), &delete_watch);
        } else {
            ::dispatch_sync_f(queue_, stream_, &stop_stream);
            ::FSEventStreamRelease(stream_);
        }
        ::dispatch_release(queue_);
    }

    FsEventsHandle(const FsEventsHandle&) = delete;
    FsEventsHandle& operator=(const FsEventsHandle&) = delete;

private:
    FSEventStreamRef stream_;
    dispatch_queue_t queue_;
    std::unique_ptr<Watch> watch_;
};

}  // namespace

struct FsEventsWatcher::Impl {
    dispatch_queue_t queue = ::dispatch_queue_create("dev.projectreboot.launcher.fsevents", DISPATCH_QUEUE_SERIAL);

    Impl() {
        ::dispatch_queue_set_specific(queue, &kQueueKey, queue, nullptr);
    }
    ~Impl() { ::dispatch_release(queue); }
    Impl(const Impl&) = delete;
    Impl& operator=(const Impl&) = delete;
};

FsEventsWatcher::FsEventsWatcher() : impl_(std::make_unique<Impl>()) {}

FsEventsWatcher::~FsEventsWatcher() = default;

Result<ports::WatchHandle> FsEventsWatcher::watch(const NativePath& dir, UniqueFunction<void(ports::FileChange)> on_change) {
    std::unique_ptr<char, decltype(&std::free)> real{::realpath(dir.c_str(), nullptr), &std::free};
    if (!real) return std::unexpected(posix::call_failed("realpath", errno, dir));
    auto state = std::make_unique<Watch>();
    state->dir = dir;
    state->real_dir = NativePath{std::string(real.get())};
    state->on_change = std::move(on_change);

    CfPtr<CFStringRef> path = cf_string(state->real_dir.native());
    if (!path)
        return make_diag(ErrorDomain::Platform, kCallFailedOnPath).arg("call", "FSEventStreamCreate").arg("path", dir).fail();
    const void* path_values[] = {path.get()};
    CfPtr<CFArrayRef> paths{::CFArrayCreate(kCFAllocatorDefault, path_values, 1, &kCFTypeArrayCallBacks)};
    FSEventStreamContext context{};
    context.info = state.get();
    FSEventStreamRef stream =
        ::FSEventStreamCreate(kCFAllocatorDefault, &on_events, &context, paths.get(), kFSEventStreamEventIdSinceNow,
                              kLatencySeconds, kFSEventStreamCreateFlagFileEvents | kFSEventStreamCreateFlagNoDefer);
    if (stream == nullptr)
        return make_diag(ErrorDomain::Platform, kCallFailedOnPath).arg("call", "FSEventStreamCreate").arg("path", dir).fail();
    ::FSEventStreamSetDispatchQueue(stream, impl_->queue);
    if (!::FSEventStreamStart(stream)) {
        ::FSEventStreamInvalidate(stream);
        ::FSEventStreamRelease(stream);
        return make_diag(ErrorDomain::Platform, kCallFailedOnPath).arg("call", "FSEventStreamStart").arg("path", dir).fail();
    }
    return ports::WatchHandle{std::make_unique<FsEventsHandle>(stream, impl_->queue, std::move(state))};
}

}  // namespace rb::os_macos::platform
