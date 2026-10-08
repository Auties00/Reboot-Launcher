#pragma once

#include <string_view>
#include <vector>

#include <boost/json/object.hpp>

#include "reboot/foundation/diag.hpp"
#include "reboot/foundation/types.hpp"
#include "reboot/host/host_profile.hpp"
#include "reboot/storage/load_report.hpp"

namespace reboot::host {

// data/host-profiles.json, a storage::Document. Enums are stored by name.
struct HostProfilesDocument {
    static constexpr std::string_view kName = "host-profiles";
    static constexpr u32 kSchema = 1;

    std::vector<HostProfile> profiles;
    boost::json::object unknown;

    // A profile that fails validate() is dropped with a ValueIssue, unless it is built in: then
    // its invalid members fall back to the built-in defaults. A duplicate id or name keeps the first.
    [[nodiscard]] static HostProfilesDocument read(const boost::json::object& values,
                                                   std::vector<storage::ValueIssue>& issues);
    [[nodiscard]] boost::json::object write() const;
    [[nodiscard]] static Result<boost::json::object> upgrade(boost::json::object values, u32 from_schema);
};

}  // namespace reboot::host
