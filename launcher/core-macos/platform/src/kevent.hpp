#pragma once

#include <sys/types.h>
#include <sys/event.h>

#include <cstdint>

namespace reboot::os_macos::platform {

// EV_SET without the macro, so field conversions stay explicit.
[[nodiscard]] inline struct kevent make_kevent(std::uintptr_t ident, std::int16_t filter, std::uint16_t flags,
                                               std::uint32_t fflags = 0, std::intptr_t data = 0,
                                               void* udata = nullptr) noexcept {
    struct kevent event {};
    event.ident = ident;
    event.filter = filter;
    event.flags = flags;
    event.fflags = fflags;
    event.data = data;
    event.udata = udata;
    return event;
}

}  // namespace reboot::os_macos::platform
