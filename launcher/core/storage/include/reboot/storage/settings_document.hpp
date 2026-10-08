#pragma once

#include <string_view>
#include <vector>

#include <boost/json/object.hpp>

#include "reboot/foundation/diag.hpp"
#include "reboot/foundation/types.hpp"
#include "reboot/storage/load_report.hpp"
#include "reboot/storage/settings_values.hpp"

namespace reboot::storage {

// config/settings.json: a flat object of key id -> value, each key validated on its own.
struct SettingsDocument {
    static constexpr std::string_view kName = "settings";
    static constexpr u32 kSchema = 1;

    SettingsValues values;
    // Keys this build does not know, written by a newer engine at the same schema.
    boost::json::object unknown;

    [[nodiscard]] static SettingsDocument read(const boost::json::object& values, std::vector<ValueIssue>& issues);
    [[nodiscard]] boost::json::object write() const;
    [[nodiscard]] static Result<boost::json::object> upgrade(boost::json::object values, u32 from_schema);
};

}  // namespace reboot::storage
