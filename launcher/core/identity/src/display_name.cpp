#include "reboot/identity/display_name.hpp"

#include <algorithm>

#include "messages.hpp"
#include "random_chars.hpp"

namespace reboot::identity {

namespace {

using contracts::backend::AccountRole;

constexpr std::size_t kDefaultDigits = 6;

[[nodiscard]] constexpr std::string_view default_prefix(AccountRole role) noexcept {
    return role == AccountRole::Host ? "Host" : "Player";
}

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

std::string default_display_name(AccountRole role, IRandom& random) {
    return std::string(default_prefix(role)) + random_chars(random, "0123456789", kDefaultDigits);
}

bool is_default_display_name(std::string_view name, AccountRole role) noexcept {
    const std::string_view prefix = default_prefix(role);
    if (name.size() != prefix.size() + kDefaultDigits || !name.starts_with(prefix)) return false;
    return std::ranges::all_of(name.substr(prefix.size()), [](char c) { return c >= '0' && c <= '9'; });
}

}  // namespace reboot::identity
