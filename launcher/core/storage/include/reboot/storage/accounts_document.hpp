#pragma once

#include <string>
#include <string_view>
#include <vector>

#include <boost/json/object.hpp>

#include "reboot/contracts/backend.hpp"
#include "reboot/foundation/diag.hpp"
#include "reboot/foundation/types.hpp"
#include "reboot/storage/load_report.hpp"

namespace rb::storage {

// account_id = display_name + "-" + tag. A rename changes display_name only.
struct AccountRecord {
    AccountRecordId record_id;
    contracts::backend::AccountRole role{};
    // [A-Za-z0-9]{3,16}.
    std::string display_name;
    // [a-z0-9]{6}.
    std::string tag;
    // Members this build does not know, kept for the newer engine that wrote them.
    boost::json::object unknown;

    bool operator==(const AccountRecord&) const = default;
};

// Capabilities: settings-storage.game-store.
// data/accounts.json. The identity package owns the rules; v1 keeps one record per role.
struct AccountsDocument {
    static constexpr std::string_view kName = "accounts";
    static constexpr u32 kSchema = 1;

    std::vector<AccountRecord> records;
    boost::json::object unknown;

    // A record with a bad id, role, name or tag is dropped with a ValueIssue; identity mints a new one.
    [[nodiscard]] static AccountsDocument read(const boost::json::object& values, std::vector<ValueIssue>& issues);
    [[nodiscard]] boost::json::object write() const;
    [[nodiscard]] static Result<boost::json::object> upgrade(boost::json::object values, u32 from_schema);
};

}  // namespace rb::storage
