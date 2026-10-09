#include "win32.hpp"  // first: sets _WIN32_WINNT before any std header pulls in <windows.h>

#include "reboot/os_windows/platform/windows_file_watcher.hpp"

#include <atomic>
#include <map>
#include <mutex>
#include <thread>
#include <utility>
#include <vector>

#include "reboot/foundation/log.hpp"
#include "unique_handle.hpp"
#include "wide.hpp"
#include "win_error.hpp"

namespace rb::os_windows::platform {

namespace {

constexpr ULONG_PTR kQuitKey = 0;
constexpr DWORD kNotifyFilter = FILE_NOTIFY_CHANGE_FILE_NAME | FILE_NOTIFY_CHANGE_DIR_NAME |
                                FILE_NOTIFY_CHANGE_LAST_WRITE | FILE_NOTIFY_CHANGE_SIZE | FILE_NOTIFY_CHANGE_CREATION;
// 64 KiB is the most ReadDirectoryChangesW accepts over the network.
constexpr std::size_t kBufferWords = 64 * 1024 / sizeof(DWORD);

[[nodiscard]] ports::FileChangeKind kind_of(DWORD action) noexcept {
    switch (action) {
        case FILE_ACTION_ADDED: return ports::FileChangeKind::Created;
        case FILE_ACTION_REMOVED: return ports::FileChangeKind::Removed;
        case FILE_ACTION_RENAMED_OLD_NAME:
        case FILE_ACTION_RENAMED_NEW_NAME: return ports::FileChangeKind::Renamed;
        default: return ports::FileChangeKind::Modified;
    }
}

struct Watch {
    NativePath dir;
    UniqueHandle directory;
    OVERLAPPED overlapped{};
    std::vector<DWORD> buffer = std::vector<DWORD>(kBufferWords);
    // Held while a change is delivered; a handle destroyed outside the callback waits on it.
    std::mutex delivery;
    std::atomic<bool> active{true};
    std::atomic<std::thread::id> delivering{};
    UniqueFunction<void(ports::FileChange)> on_change;

    [[nodiscard]] DWORD arm() {
        overlapped = OVERLAPPED{};
        if (ReadDirectoryChangesW(directory.get(), buffer.data(), static_cast<DWORD>(buffer.size() * sizeof(DWORD)), FALSE,
                                  kNotifyFilter, nullptr, &overlapped, nullptr) == 0)
            return GetLastError();
        return 0;
    }

    void deliver(ports::FileChange change) {
        if (!active.load(std::memory_order_acquire) || !on_change) return;
        try {
            on_change(std::move(change));
        } catch (...) {
            REBOOT_LOG_ERROR(Engine, "internal.bug: a file watcher callback threw");
        }
    }

    // Delivers what `bytes` of the buffer hold, or a rescan hint on an overflow.
    void deliver_all(DWORD bytes) {
        if (bytes == 0) {
            deliver({dir, ports::FileChangeKind::Modified});
            return;
        }
        const auto* cursor = reinterpret_cast<const std::byte*>(buffer.data());
        for (;;) {
            const auto* info = reinterpret_cast<const FILE_NOTIFY_INFORMATION*>(cursor);
            const std::wstring_view name(info->FileName, info->FileNameLength / sizeof(wchar_t));
            deliver({dir / NativePath(name), kind_of(info->Action)});
            if (info->NextEntryOffset == 0) return;
            cursor += info->NextEntryOffset;
        }
    }
};

class WatchToken final : public ports::WatchHandle::Handle {
public:
    explicit WatchToken(std::shared_ptr<Watch> watch) noexcept : watch_(std::move(watch)) {}

    ~WatchToken() override {
        watch_->active.store(false, std::memory_order_release);
        // Inside its own callback the delivery lock is already ours.
        if (watch_->delivering.load(std::memory_order_acquire) != std::this_thread::get_id()) {
            std::scoped_lock lock(watch_->delivery);
        }
        CancelIoEx(watch_->directory.get(), &watch_->overlapped);
    }

private:
    std::shared_ptr<Watch> watch_;
};

}  // namespace

struct WindowsFileWatcher::Impl {
    UniqueHandle port;
    std::thread thread;
    std::mutex mutex;
    // Watches with a read in flight, owned here until its completion arrives.
    std::map<Watch*, std::shared_ptr<Watch>> pending;
    bool stopping = false;

    // Here rather than in the watcher, so a move-assignment ends the thread it replaces.
    ~Impl() {
        if (!thread.joinable()) return;
        PostQueuedCompletionStatus(port.get(), 0, kQuitKey, nullptr);
        thread.join();
    }

    void run() {
        for (;;) {
            DWORD bytes = 0;
            ULONG_PTR key = 0;
            OVERLAPPED* overlapped = nullptr;
            const BOOL ok = GetQueuedCompletionStatus(port.get(), &bytes, &key, &overlapped, INFINITE);
            if (overlapped == nullptr) {
                if (key != kQuitKey) continue;
                std::scoped_lock lock(mutex);
                stopping = true;
                for (const auto& [raw, watch] : pending) CancelIoEx(watch->directory.get(), &watch->overlapped);
                if (pending.empty()) return;
                continue;
            }
            auto* raw = reinterpret_cast<Watch*>(key);
            std::shared_ptr<Watch> watch;
            {
                std::scoped_lock lock(mutex);
                const auto found = pending.find(raw);
                if (found == pending.end()) continue;
                watch = found->second;
            }
            const DWORD error = ok ? 0 : GetLastError();
            bool rearm = false;
            {
                std::scoped_lock lock(watch->delivery);
                watch->delivering.store(std::this_thread::get_id(), std::memory_order_release);
                if (error == 0 || error == ERROR_NOTIFY_ENUM_DIR) {
                    watch->deliver_all(error == 0 ? bytes : 0);
                    rearm = true;
                } else if (error != ERROR_OPERATION_ABORTED) {
                    // The directory went away, or its volume did.
                    watch->deliver({watch->dir, ports::FileChangeKind::Removed});
                }
                if (rearm && watch->active.load(std::memory_order_acquire)) {
                    std::scoped_lock pending_lock(mutex);
                    if (!stopping && watch->arm() == 0) {
                        watch->delivering.store(std::thread::id{}, std::memory_order_release);
                        continue;
                    }
                    if (!stopping) watch->deliver({watch->dir, ports::FileChangeKind::Removed});
                }
                watch->delivering.store(std::thread::id{}, std::memory_order_release);
            }
            std::scoped_lock lock(mutex);
            pending.erase(raw);
            if (stopping && pending.empty()) return;
        }
    }
};

Result<WindowsFileWatcher> WindowsFileWatcher::create() {
    auto impl = std::make_unique<Impl>();
    impl->port.reset(CreateIoCompletionPort(INVALID_HANDLE_VALUE, nullptr, 0, 1));
    if (!impl->port) return std::unexpected(call_failed("CreateIoCompletionPort", GetLastError()));
    Impl* raw = impl.get();
    try {
        impl->thread = std::thread([raw] { raw->run(); });
    } catch (...) {
        return std::unexpected(call_failed("CreateThread", ERROR_NOT_ENOUGH_MEMORY));
    }
    return WindowsFileWatcher(std::move(impl));
}

WindowsFileWatcher::WindowsFileWatcher(std::unique_ptr<Impl> impl) noexcept : impl_(std::move(impl)) {}
WindowsFileWatcher::WindowsFileWatcher(WindowsFileWatcher&&) noexcept = default;
WindowsFileWatcher& WindowsFileWatcher::operator=(WindowsFileWatcher&&) noexcept = default;

WindowsFileWatcher::~WindowsFileWatcher() = default;

Result<ports::WatchHandle> WindowsFileWatcher::watch(const NativePath& dir, UniqueFunction<void(ports::FileChange)> on_change) {
    auto watch = std::make_shared<Watch>();
    watch->dir = dir;
    watch->on_change = std::move(on_change);
    const std::wstring target = extended_path(dir);
    watch->directory.reset(CreateFileW(target.c_str(), FILE_LIST_DIRECTORY,
                                       FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
                                       FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OVERLAPPED, nullptr));
    if (!watch->directory) return std::unexpected(call_failed("CreateFileW", GetLastError(), dir));
    if (CreateIoCompletionPort(watch->directory.get(), impl_->port.get(), reinterpret_cast<ULONG_PTR>(watch.get()), 0) ==
        nullptr)
        return std::unexpected(call_failed("CreateIoCompletionPort", GetLastError(), dir));
    {
        std::scoped_lock lock(impl_->mutex);
        if (const DWORD error = watch->arm(); error != 0)
            return std::unexpected(call_failed("ReadDirectoryChangesW", error, dir));
        impl_->pending.emplace(watch.get(), watch);
    }
    return ports::WatchHandle(std::make_unique<WatchToken>(std::move(watch)));
}

}  // namespace rb::os_windows::platform
