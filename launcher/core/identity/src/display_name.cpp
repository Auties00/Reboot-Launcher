#include "reboot/identity/display_name.hpp"

#include <algorithm>

#include "messages.hpp"
#include "random_chars.hpp"

namespace rb::identity {

namespace {

constexpr std::size_t kDefaultDigits = 6;
constexpr std::string_view kDefaultPrefix = "Player";

}  // namespace

Result<void> validate_display_name(std::string_view name) {
    if (name.size() < kMinDisplayNameLength)
        return make_diag(ErrorDomain::Identity, msg::kDisplayNameTooShort)
            .kind(ErrorKind::InvalidInput)
            .arg("min", kMinDisplayNameLength)
            .fail();
    if (name.size() > kMaxDisplayNameLength)
        return make_diag(ErrorDomain::Identity, msg::kDisplayNameTooLong)
            .kind(ErrorKind::InvalidInput)
            .arg("max", kMaxDisplayNameLength)
            .fail();
    if (!std::ranges::all_of(name, is_ascii_alnum))
        return make_diag(ErrorDomain::Identity, msg::kDisplayNameInvalidCharacter).kind(ErrorKind::InvalidInput).fail();
    return {};
}

std::string default_display_name(IRandom& random) {
    return std::string(kDefaultPrefix) + random_chars(random, "0123456789", kDefaultDigits);
}

bool is_default_display_name(std::string_view name) noexcept {
    if (name.size() != kDefaultPrefix.size() + kDefaultDigits || !name.starts_with(kDefaultPrefix)) return false;
    return std::ranges::all_of(name.substr(kDefaultPrefix.size()), [](char c) { return c >= '0' && c <= '9'; });
}

}  // namespace rb::identity
