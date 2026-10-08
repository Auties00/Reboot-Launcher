#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <boost/json/object.hpp>

#include "reboot/foundation/diag.hpp"
#include "reboot/foundation/types.hpp"
#include "reboot/foundation/version.hpp"
#include "reboot/ports/os_services.hpp"
#include "reboot/storage/load_report.hpp"
#include "reboot/storage/shell_name.hpp"

namespace reboot::storage {

// Each frontend has its own tour: finishing it in the CLI leaves it offered in WinUI.
struct ShellOnboarding {
    ShellName shell;
    // Finished or dismissed; clearing it offers the tour again.
    bool completed = false;
    std::vector<std::string> completed_steps;

    bool operator==(const ShellOnboarding&) const = default;
};

// Once a host was seen on https it is never downgraded; plain http needs a remembered answer.
struct UpstreamTlsMemory {
    std::string host;
    bool https_seen = false;
    bool http_acknowledged = false;

    bool operator==(const UpstreamTlsMemory&) const = default;
};

// Capabilities: settings-storage.app-store.
// state/state.json: engine bookkeeping that is neither a setting nor exported, and that resets never touch.
struct StateDocument {
    static constexpr std::string_view kName = "state";
    static constexpr u32 kSchema = 1;

    // The only record of first-run completion, one entry per shell.
    std::vector<ShellOnboarding> onboarding;
    std::vector<std::string> dismissed_notices;
    std::vector<UpstreamTlsMemory> upstream_tls;
    std::vector<ports::IntegrationKind> declined_integrations;
    // Integration is reconciled at startup when this differs from the running version.
    std::optional<SemVer> last_run_version;
    boost::json::object unknown;

    [[nodiscard]] static StateDocument read(const boost::json::object& values, std::vector<ValueIssue>& issues);
    [[nodiscard]] boost::json::object write() const;
    [[nodiscard]] static Result<boost::json::object> upgrade(boost::json::object values, u32 from_schema);
};

}  // namespace reboot::storage
