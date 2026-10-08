#include "reboot/identity/legacy_login.hpp"

#include "random_chars.hpp"
#include "reboot/identity/account_record.hpp"

namespace reboot::identity {

LegacyLogin legacy_derive(std::string_view username, bool has_password) {
    if (has_password) return {std::string(username), LegacyPassword::UserPassword};
    std::string name = keep_ascii_alnum(username);
    if (name.empty()) name = "Player";
    return {name + std::string(kLoginDomainSuffix), LegacyPassword::Placeholder};
}

}  // namespace reboot::identity
