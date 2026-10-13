#pragma once

#include <string_view>
#include <vector>

#include <boost/json/object.hpp>

#include "reboot/base/diag.hpp"
#include "reboot/base/native_path.hpp"
#include "reboot/base/types.hpp"
#include "reboot/compat/prefix_record.hpp"
#include "reboot/compat/runtime_record.hpp"
#include "reboot/foundation/paths.hpp"
#include "reboot/storage/load_report.hpp"

namespace rb::compat {

// Covers no capability ids (decisions persistence-format-migration, update-mechanism).
// data/prefixes/compat.json: engine state shared by RuntimeService and PrefixManager. A runtime
// record goes at the next record write once the component store no longer lists its runtime.
struct CompatDocument {
    static constexpr std::string_view kName = "compat";
    static constexpr u32 kSchema = 1;

    std::vector<PrefixRecord> prefixes;
    std::vector<RuntimeRecord> runtimes;
    boost::json::object unknown;

    // A bad record is dropped with a ValueIssue, the rest are kept.
    [[nodiscard]] static CompatDocument read(const boost::json::object& values,
                                             std::vector<storage::ValueIssue>& issues);
    [[nodiscard]] boost::json::object write() const;
    [[nodiscard]] static Result<boost::json::object> upgrade(boost::json::object values, u32 from_schema);
};

[[nodiscard]] inline NativePath compat_document_path(const AppLayout& layout) {
    return layout.prefixes_dir() / "compat.json";
}

}  // namespace rb::compat
