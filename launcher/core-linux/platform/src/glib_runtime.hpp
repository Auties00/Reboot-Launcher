#pragma once

#include <chrono>
#include <condition_variable>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <thread>

#include "reboot/foundation/types.hpp"

namespace reboot::os_linux::platform {

// struct _GError, whose layout is GLib's stable ABI.
struct GErrorView {
    u32 domain;
    int code;
    char* message;
};

// The GLib, GObject and GIO calls both dlopen'ed adapters make, found through the library that
// loaded them. The library is never closed: GLib cannot be unloaded once its threads run.
struct GLibRuntime {
    void* library = nullptr;
    void (*error_free)(GErrorView*) = nullptr;
    const char* (*quark_to_string)(u32) = nullptr;
    void (*object_unref)(void*) = nullptr;
    void* (*cancellable_new)() = nullptr;
    void (*cancellable_cancel)(void*) = nullptr;

    // dlopen(`soname`) and the calls above; nullopt when the library or a symbol is missing.
    [[nodiscard]] static std::optional<GLibRuntime> load(const char* soname);

    // The address of `name` in the library, or nullptr.
    [[nodiscard]] void* symbol(const char* name) const noexcept;

    // "<domain> <code>" for Diagnostic::detail; frees `error`.
    [[nodiscard]] std::string take_error(GErrorView* error) const;
};

// A function pointer of type T for `name`; false when missing.
template <class T>
[[nodiscard]] bool resolve(const GLibRuntime& runtime, const char* name, T& out) noexcept {
    void* const address = runtime.symbol(name);
    if (address == nullptr) return false;
    out = reinterpret_cast<T>(address);
    return true;
}

// Cancels a GCancellable once `deadline` passes, unless destroyed first.
class CancelAfter {
public:
    CancelAfter(const GLibRuntime& runtime, void* cancellable, std::chrono::milliseconds deadline);
    ~CancelAfter();
    CancelAfter(const CancelAfter&) = delete;
    CancelAfter& operator=(const CancelAfter&) = delete;

    // Whether the deadline cancelled the call. Read it after the call returned.
    [[nodiscard]] bool fired() const;

private:
    mutable std::mutex mutex_;
    std::condition_variable changed_;
    bool finished_ = false;
    bool fired_ = false;
    std::thread timer_;
};

// A GCancellable owned for one call.
class Cancellable {
public:
    explicit Cancellable(const GLibRuntime& runtime) : runtime_(runtime), object_(runtime.cancellable_new()) {}
    ~Cancellable() {
        if (object_ != nullptr) runtime_.object_unref(object_);
    }
    Cancellable(const Cancellable&) = delete;
    Cancellable& operator=(const Cancellable&) = delete;

    [[nodiscard]] void* get() const noexcept { return object_; }

private:
    const GLibRuntime& runtime_;
    void* object_;
};

}  // namespace reboot::os_linux::platform
