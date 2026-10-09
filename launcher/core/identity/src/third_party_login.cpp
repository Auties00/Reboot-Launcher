#include "reboot/identity/third_party_login.hpp"

#include "random_chars.hpp"
#include "reboot/identity/account_record.hpp"

namespace reboot::identity {

std::string third_party_login(std::string_view name) {
    std::string login = keep_ascii_alnum(name);
    if (login.empty()) login = "Player";
    return login + std::string(kLoginDomainSuffix);
}

}  // namespace reboot::identity
