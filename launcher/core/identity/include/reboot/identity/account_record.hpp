#pragma once

#include <cstddef>
#include <string>
#include <string_view>

#include "reboot/contracts/backend.hpp"
#include "reboot/identity/display_name.hpp"
#include "reboot/storage/accounts_document.hpp"

namespace reboot {
class IRandom;
}

namespace reboot::identity {

using AccountRole = contracts::backend::AccountRole;
// storage owns the shape of data/accounts.json; this package owns its rules.
using AccountRecord = storage::AccountRecord;

inline constexpr std::size_t kTagLength = 6;
inline constexpr std::size_t kMaxAccountIdLength = kMaxDisplayNameLength + 1 + kTagLength;

// Appended when no password is set, as 10.0.9 did.
inline constexpr std::string_view kLoginDomainSuffix = "@projectreboot.dev";

// display_name + "-" + tag, at most 23 chars. The tag makes it unique without a registry, so two
// players with the same name no longer kick each other off XMPP.
[[nodiscard]] std::string account_id(const AccountRecord& record);

// 6 chars of [a-z0-9] from the CSPRNG; minted once per record and kept across renames.
[[nodiscard]] std::string generate_tag(IRandom& random);

}  // namespace reboot::identity
