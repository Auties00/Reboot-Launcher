#include "reboot/os_linux/platform/inotify_watcher.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <cerrno>
#include <cstring>
#include <exception>
#include <mutex>
#include <poll.h>
#include <sys/eventfd.h>
#include <sys/inotify.h>
#include <thread>
#include <unistd.h>
#include <unordered_map>
#include <utility>
#include <vector>

#include "inotify_events.hpp"
#include "messages.hpp"
#include "reboot/foundation/log.hpp"
#include "reboot/posix/posix_error.hpp"
#include "reboot/posix/unique_fd.hpp"

namespace reboot::os_linux::platform {

namespace {

// Room for many events at once; each is a header plus a NUL-padded name.
constexpr std::size_t kReadBuffer = 64 * 1024;

struct Subscriber {
    explicit Subscriber(UniqueFunction<void(ports::FileChange)> callback) : on_change(std::move(callback)) {}

    // Held while on_change runs, so a handle destroyed on another thread waits for it.
    std::mutex running;
    std::atomic<bool> active{true};
    UniqueFunction<void(ports::FileChange)> on_change;
};

struct Watch {
    NativePath dir;
    std::vector<std::shared_ptr<Subscriber>> subscribers;
};

// Outlives the watcher while handles remain, so a late handle still finds its watch table.
struct Shared {
    posix::UniqueFd inotify;
    posix::UniqueFd wake;
    std::thread::id reader;
    std::mutex mutex;
    std::unordered_map<int, Watch> watches;
};

struct Delivery {
    std::shared_ptr<Subscriber> subscriber;
    ports::FileChange change;
};

class InotifyWatchHandle final : public ports::WatchHandle::Handle {
public:
    InotifyWatchHandle(std::shared_ptr<Shared> shared, int wd, std::shared_ptr<Subscriber> subscriber)
        : shared_(std::move(shared)), wd_(wd), subscriber_(std::move(subscriber)) {}

    ~InotifyWatchHandle() override {
        // On the reader thread the callback is either this caller or not running at all.
        if (std::this_thread::get_id() == shared_->reader) {
            subscriber_->active = false;
        } else {
            const std::lock_guard running(subscriber_->running);
            subscriber_->active = false;
        }
        const std::lock_guard lock(shared_->mutex);
        const auto found = shared_->watches.find(wd_);
        if (found == shared_->watches.end()) return;
        std::erase(found->second.subscribers, subscriber_);
        if (!found->second.subscribers.empty()) return;
        ::inotify_rm_watch(shared_->inotify.get(), wd_);
        shared_->watches.erase(found);
    }

    InotifyWatchHandle(const InotifyWatchHandle&) = delete;
    InotifyWatchHandle& operator=(const InotifyWatchHandle&) = delete;

private:
    std::shared_ptr<Shared> shared_;
    int wd_;
    std::shared_ptr<Subscriber> subscriber_;
};

void collect(Shared& shared, int wd, u32 mask, std::string_view name, std::vector<Delivery>& out) {
    if ((mask & IN_IGNORED) != 0) {
        shared.watches.erase(wd);
        return;
    }
    const std::optional<InotifyChange> change = map_inotify_event(mask);
    if (!change) return;
    const auto deliver = [&](const Watch& target, const NativePath& path) {
        for (const auto& subscriber : target.subscribers) out.push_back({subscriber, {path, change->kind}});
    };
    if (change->subject == InotifySubject::AllDirectories) {
        for (const auto& entry : shared.watches) deliver(entry.second, entry.second.dir);
        return;
    }
    const auto found = shared.watches.find(wd);
    if (found == shared.watches.end()) return;
    if (change->subject == InotifySubject::Directory) {
        deliver(found->second, found->second.dir);
    } else if (!name.empty()) {
        deliver(found->second, found->second.dir / name);
    }
}

void dispatch(std::vector<Delivery>& deliveries) {
    for (Delivery& delivery : deliveries) {
        Subscriber& subscriber = *delivery.subscriber;
        const std::lock_guard running(subscriber.running);
        if (!subscriber.active) continue;
        try {
            subscriber.on_change(std::move(delivery.change));
        } catch (const std::exception& error) {
            REBOOT_LOG_ERROR(Engine, "internal.bug: a file watch callback threw: {}", error.what());
        } catch (...) {
            REBOOT_LOG_ERROR(Engine, "internal.bug: a file watch callback threw");
        }
    }
    deliveries.clear();
}

void read_events(Shared& shared) {
    alignas(inotify_event) std::array<char, kReadBuffer> buffer{};
    std::vector<Delivery> deliveries;
    for (;;) {
        std::array<pollfd, 2> fds{{{.fd = shared.inotify.get(), .events = POLLIN, .revents = 0},
                                   {.fd = shared.wake.get(), .events = POLLIN, .revents = 0}}};
        if (::poll(fds.data(), fds.size(), -1) < 0) {
            if (errno == EINTR) continue;
            REBOOT_LOG_ERROR(Engine, "inotify poll failed with errno {}; file watches stop", errno);
            return;
        }
        if (fds[1].revents != 0) return;
        const ssize_t got = ::read(shared.inotify.get(), buffer.data(), buffer.size());
        if (got < 0) {
            if (errno == EINTR || errno == EAGAIN) continue;
            REBOOT_LOG_ERROR(Engine, "inotify read failed with errno {}; file watches stop", errno);
            return;
        }
        {
            const std::lock_guard lock(shared.mutex);
            std::size_t offset = 0;
            const auto total = static_cast<std::size_t>(got);
            while (offset + sizeof(inotify_event) <= total) {
                inotify_event event{};
                std::memcpy(&event, buffer.data() + offset, sizeof event);
                const char* const name_bytes = buffer.data() + offset + sizeof event;
                const std::size_t name_room = std::min<std::size_t>(event.len, total - offset - sizeof event);
                const std::string_view name{name_bytes, ::strnlen(name_bytes, name_room)};
                collect(shared, event.wd, event.mask, name, deliveries);
                offset += sizeof event + event.len;
            }
        }
        dispatch(deliveries);
    }
}

}  // namespace

struct InotifyWatcher::Impl {
    std::shared_ptr<Shared> shared = std::make_shared<Shared>();
    std::optional<Diagnostic> init_error;
    std::thread reader;
};

InotifyWatcher::InotifyWatcher() : impl_(std::make_unique<Impl>()) {
    Shared& shared = *impl_->shared;
    shared.inotify.reset(::inotify_init1(IN_CLOEXEC | IN_NONBLOCK));
    if (!shared.inotify.valid()) {
        impl_->init_error = posix::call_failed("inotify_init1", errno);
        return;
    }
    shared.wake.reset(::eventfd(0, EFD_CLOEXEC | EFD_NONBLOCK));
    if (!shared.wake.valid()) {
        impl_->init_error = posix::call_failed("eventfd", errno);
        return;
    }
    impl_->reader = std::thread([shared_state = impl_->shared] {
        try {
            read_events(*shared_state);
        } catch (...) {
            REBOOT_LOG_ERROR(Engine, "internal.bug: the inotify reader thread failed");
        }
    });
    shared.reader = impl_->reader.get_id();
}

InotifyWatcher::~InotifyWatcher() {
    if (!impl_->reader.joinable()) return;
    const u64 one = 1;
    while (::write(impl_->shared->wake.get(), &one, sizeof one) < 0 && errno == EINTR) {
    }
    impl_->reader.join();
}

Result<ports::WatchHandle> InotifyWatcher::watch(const NativePath& dir, UniqueFunction<void(ports::FileChange)> on_change) {
    if (impl_->init_error) return std::unexpected(*impl_->init_error);
    Shared& shared = *impl_->shared;
    auto subscriber = std::make_shared<Subscriber>(std::move(on_change));
    const std::lock_guard lock(shared.mutex);
    const int wd = ::inotify_add_watch(shared.inotify.get(), dir.c_str(), inotify_watch_mask());
    if (wd < 0) {
        if (errno == ENOSPC) return make_diag(ErrorDomain::Platform, kWatchLimit).retryable().fail();
        return std::unexpected(posix::call_failed("inotify_add_watch", errno, dir));
    }
    Watch& entry = shared.watches[wd];
    if (entry.subscribers.empty()) entry.dir = dir;
    entry.subscribers.push_back(subscriber);
    return ports::WatchHandle{std::make_unique<InotifyWatchHandle>(impl_->shared, wd, std::move(subscriber))};
}

}  // namespace reboot::os_linux::platform
