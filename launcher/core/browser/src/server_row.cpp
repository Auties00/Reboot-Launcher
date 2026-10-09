#include "reboot/browser/server_row.hpp"

#include "reboot/foundation/text.hpp"
#include "wire_mapping.hpp"

namespace reboot::browser {

ServerRow make_server_row(const sb::wire::ListEntry& entry, std::chrono::milliseconds clock_offset) {
    namespace flag = sb::wire::entry_flag;
    ServerRow row;
    row.id = ServerId{entry.id};
    row.name = sanitize_display_text(entry.name);
    row.author = sanitize_display_text(entry.author);
    row.version = sanitize_display_text(entry.version);
    row.bucket = entry.bucket;
    row.players = entry.players;
    row.max_players = entry.max_players;
    row.region = from_wire(entry.region);
    row.has_password = (entry.flags & flag::has_password) != 0;
    row.reachable = (entry.flags & flag::reachable) != 0;
    row.online = (entry.flags & flag::online) != 0;
    row.hidden = (entry.flags & flag::hidden) != 0;
    row.created_at = local_time(entry.created_ms, clock_offset);
    return row;
}

ServerDetails make_server_details(const sb::wire::EntryDetails& details, std::chrono::milliseconds clock_offset) {
    return ServerDetails{make_server_row(details.entry, clock_offset), sanitize_display_text(details.description),
                         local_time(details.updated_ms, clock_offset)};
}

}  // namespace reboot::browser
