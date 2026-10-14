#pragma once

#include <string>
#include <string_view>
#include <vector>

#include <boost/json/object.hpp>

#include "reboot/base/diag.hpp"
#include "reboot/base/types.hpp"
#include "reboot/contracts/backend.hpp"
#include "reboot/storage/load_report.hpp"

namespace rb::storage {

// One role's account; its account_id is the document's display_name + "-" + tag.
struct StoredAccount {
    AccountRecordId record_id;
    contracts::backend::AccountRole role{};
    // [a-z0-9]{6}, or empty when the stored one was not.
    std::string tag;
    // Members this build does not know, kept for the newer engine that wrote them.
    boost::json::object unknown;

    bool operator==(const StoredAccount&) const = default;
};

// Capabilities: settings-storage.game-store.
// data/accounts.json. The identity package owns the rules: one player name shared by one record per role.
// Schema 2 moved the name out of the records; the upgrade keeps the client's.
struct AccountsDocument {
    static constexpr std::string_view kName = "accounts";
    static constexpr u32 kSchema = 2;

    // [A-Za-z0-9]{3,16}, or empty when the stored one was not.
    std::string display_name;
    std::vector<StoredAccount> records;
    boost::json::object unknown;

    // A record with a bad id or role is dropped with a ValueIssue; a bad name or tag is left empty with
    // one, and identity repairs it.
    [[nodiscard]] static AccountsDocument read(const boost::json::object& values, std::vector<ValueIssue>& issues);
    [[nodiscard]] boost::json::object write() const;
    [[nodiscard]] static Result<boost::json::object> upgrade(boost::json::object values, u32 from_schema);
};

}  // namespace rb::storage
