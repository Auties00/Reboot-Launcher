#include "reboot/identity/account_record.hpp"

#include "random_chars.hpp"

namespace reboot::identity {

std::string account_id(const AccountRecord& record) { return record.display_name + "-" + record.tag; }

std::string generate_tag(IRandom& random) {
    return random_chars(random, "abcdefghijklmnopqrstuvwxyz0123456789", kTagLength);
}

}  // namespace reboot::identity
