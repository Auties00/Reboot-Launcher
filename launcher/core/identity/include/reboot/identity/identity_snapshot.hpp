#pragma once

#include <string>

#include "reboot/identity/account_record.hpp"

namespace rb::identity {

// One player name, used for playing and hosting, and one account per role named after it.
struct IdentitySnapshot {
    AccountRecord client;
    AccountRecord host;

    // Both records carry it.
    [[nodiscard]] const std::string& display_name() const noexcept { return client.display_name; }

    bool operator==(const IdentitySnapshot&) const = default;
};

}  // namespace rb::identity
