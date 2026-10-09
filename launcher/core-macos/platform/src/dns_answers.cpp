#include "dns_answers.hpp"

#include <algorithm>

#include "reboot/foundation/text.hpp"

namespace reboot::os_macos::platform {

namespace {

// `host` is `label` or ends in ".<label>", in any letter case.
[[nodiscard]] bool within(std::string_view host, std::string_view label) noexcept {
    if (host.size() < label.size() || !iequals_ascii(host.substr(host.size() - label.size()), label)) return false;
    return host.size() == label.size() || host[host.size() - label.size() - 1] == '.';
}

}  // namespace

SpecialName special_name(std::string_view host) noexcept {
    if (host.ends_with('.')) host.remove_suffix(1);
    if (within(host, "localhost")) return SpecialName::Loopback;
    if (within(host, "invalid")) return SpecialName::Invalid;
    return SpecialName::None;
}

void DnsAnswers::add(const IpAddress& address) {
    none(address.is_v4() ? DnsFamily::V4 : DnsFamily::V6);
    if (std::ranges::find(addresses_, address) == addresses_.end()) addresses_.push_back(address);
}

void DnsAnswers::none(DnsFamily family) noexcept {
    if (family == DnsFamily::V4) {
        v4_answered_ = true;
    } else {
        v6_answered_ = true;
    }
}

}  // namespace reboot::os_macos::platform
