#pragma once

#include <CoreFoundation/CoreFoundation.h>

#include <memory>
#include <string_view>
#include <type_traits>

namespace reboot::os_macos::platform {

struct CfReleaser {
    void operator()(CFTypeRef ref) const noexcept {
        if (ref != nullptr) ::CFRelease(ref);
    }
};

// Owns one Core Foundation reference from a Create or Copy call.
template <class Ref>
using CfPtr = std::unique_ptr<std::remove_pointer_t<Ref>, CfReleaser>;

// Nullptr when `text` is not UTF-8.
[[nodiscard]] inline CfPtr<CFStringRef> cf_string(std::string_view text) {
    return CfPtr<CFStringRef>{::CFStringCreateWithBytes(kCFAllocatorDefault, reinterpret_cast<const UInt8*>(text.data()),
                                                        static_cast<CFIndex>(text.size()), kCFStringEncodingUTF8, false)};
}

}  // namespace reboot::os_macos::platform
