#include "glib_runtime.hpp"

#include <dlfcn.h>

namespace reboot::os_linux::platform {

std::optional<GLibRuntime> GLibRuntime::load(const char* soname) {
    GLibRuntime runtime;
    runtime.library = ::dlopen(soname, RTLD_NOW | RTLD_LOCAL);
    if (runtime.library == nullptr) return std::nullopt;
    const bool complete = resolve(runtime, "g_error_free", runtime.error_free) &&
                          resolve(runtime, "g_quark_to_string", runtime.quark_to_string) &&
                          resolve(runtime, "g_object_unref", runtime.object_unref) &&
                          resolve(runtime, "g_cancellable_new", runtime.cancellable_new) &&
                          resolve(runtime, "g_cancellable_cancel", runtime.cancellable_cancel);
    if (!complete) return std::nullopt;
    return runtime;
}

void* GLibRuntime::symbol(const char* name) const noexcept {
    // A handle's lookup also searches the libraries it pulled in, which is where GLib lives.
    return ::dlsym(library, name);
}

std::string GLibRuntime::take_error(GErrorView* error) const {
    if (error == nullptr) return {};
    const char* const domain = quark_to_string(error->domain);
    std::string detail = std::string(domain != nullptr ? domain : "unknown") + " " + std::to_string(error->code);
    error_free(error);
    return detail;
}

CancelAfter::CancelAfter(const GLibRuntime& runtime, void* cancellable, std::chrono::milliseconds deadline)
    : timer_([this, &runtime, cancellable, deadline] {
          std::unique_lock lock(mutex_);
          if (changed_.wait_for(lock, deadline, [&] { return finished_; })) return;
          fired_ = true;
          lock.unlock();
          runtime.cancellable_cancel(cancellable);
      }) {}

CancelAfter::~CancelAfter() {
    {
        const std::lock_guard lock(mutex_);
        finished_ = true;
    }
    changed_.notify_all();
    timer_.join();
}

bool CancelAfter::fired() const {
    const std::lock_guard lock(mutex_);
    return fired_;
}

}  // namespace reboot::os_linux::platform
