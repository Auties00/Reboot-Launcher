#pragma once

#include <chrono>
#include <optional>
#include <string>

#include "reboot/browser/view_spec.hpp"
#include "reboot/foundation/types.hpp"
#include "wire/messages.hpp"

namespace reboot::browser {

// Capabilities: server-browser.+30.
// One server as a UI shows it. Every text field is untrusted and already passed through
// sanitize_display_text; UIs still render each in its own isolated element. Labels such as the
// "Fortnite " version prefix and the default name are the UI's.
struct ServerRow {
    ServerId id;
    std::string name;
    std::string author;
    std::string version;
    u32 bucket = 0;
    u32 players = 0;
    u32 max_players = 0;
    Region region = Region::All;
    bool has_password = false;
    bool reachable = false;
    bool online = false;
    bool hidden = false;
    // Edge time moved onto the local clock with EdgeSession::clock_offset.
    std::chrono::system_clock::time_point created_at;

    bool operator==(const ServerRow&) const = default;
};

// From Resolve, which also answers for hidden and offline servers. ListEntry has no description.
struct ServerDetails {
    ServerRow row;
    std::string description;
    std::chrono::system_clock::time_point updated_at;

    bool operator==(const ServerDetails&) const = default;
};

[[nodiscard]] ServerRow make_server_row(const sb::wire::ListEntry& entry, std::chrono::milliseconds clock_offset);
[[nodiscard]] ServerDetails make_server_details(const sb::wire::EntryDetails& details,
                                                std::chrono::milliseconds clock_offset);

}  // namespace reboot::browser
