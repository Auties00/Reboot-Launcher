#pragma once

#include <array>
#include <string_view>
#include <vector>

#include <boost/json/object.hpp>

#include "reboot/foundation/diag.hpp"
#include "reboot/foundation/native_path.hpp"
#include "reboot/foundation/paths.hpp"
#include "reboot/foundation/types.hpp"
#include "reboot/identity/backend_login.hpp"
#include "reboot/storage/enum_names.hpp"
#include "reboot/storage/load_report.hpp"

namespace reboot::storage {

template <>
struct EnumNames<identity::CredentialPolicy> {
    static constexpr std::array<std::string_view, 2> kNames{"ticket", "legacy_argv"};
};

}  // namespace reboot::storage

namespace reboot::identity {

// Capabilities: profile-identity.credentials.
// data/backend-logins.json, a storage::Document. An entry equal to the defaults, with no unknown
// members, is not kept.
struct BackendLoginsDocument {
    static constexpr std::string_view kName = "backend-logins";
    static constexpr u32 kSchema = 1;

    std::vector<BackendLogin> logins;
    boost::json::object unknown;

    // An entry with a bad endpoint, a bad or empty login or an endpoint already read is dropped
    // with a ValueIssue; a bad policy reads as Ticket with one.
    [[nodiscard]] static BackendLoginsDocument read(const boost::json::object& values,
                                                    std::vector<storage::ValueIssue>& issues);
    [[nodiscard]] boost::json::object write() const;
    [[nodiscard]] static Result<boost::json::object> upgrade(boost::json::object values, u32 from_schema);
};

[[nodiscard]] inline NativePath backend_logins_document_path(const AppLayout& layout) {
    return layout.root() / "data" / "backend-logins.json";
}

}  // namespace reboot::identity
