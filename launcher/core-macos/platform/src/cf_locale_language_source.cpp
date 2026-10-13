#include "darwin.hpp"

#include "reboot/os_macos/platform/cf_locale_language_source.hpp"

#include <CoreFoundation/CoreFoundation.h>

#include <string>
#include <utility>

#include "cf_ptr.hpp"

namespace rb::os_macos::platform {

namespace {

// Empty when `text` cannot be converted.
[[nodiscard]] std::string utf8_of(CFStringRef text) {
    const CFIndex capacity = ::CFStringGetMaximumSizeForEncoding(::CFStringGetLength(text), kCFStringEncodingUTF8) + 1;
    std::string out(static_cast<std::size_t>(capacity), '\0');
    if (!::CFStringGetCString(text, out.data(), capacity, kCFStringEncodingUTF8)) return {};
    out.resize(std::char_traits<char>::length(out.c_str()));
    return out;
}

}  // namespace

std::vector<std::string> CfLocaleLanguageSource::preferred_ui_languages() {
    std::vector<std::string> out;
    const CfPtr<CFArrayRef> languages{::CFLocaleCopyPreferredLanguages()};
    if (!languages) return out;
    const CFIndex count = ::CFArrayGetCount(languages.get());
    for (CFIndex i = 0; i < count; ++i) {
        const CFTypeRef value = ::CFArrayGetValueAtIndex(languages.get(), i);
        if (value == nullptr || ::CFGetTypeID(value) != ::CFStringGetTypeID()) continue;
        if (std::string tag = utf8_of(static_cast<CFStringRef>(value)); !tag.empty()) out.push_back(std::move(tag));
    }
    return out;
}

}  // namespace rb::os_macos::platform
