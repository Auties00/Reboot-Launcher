#include "reboot/browser/list_state.hpp"

namespace reboot::browser {

std::size_t ViewUpdate::approx_bytes() const noexcept {
    std::size_t bytes = sizeof(ViewUpdate) + (rows.size() * sizeof(ServerRow));
    for (const ServerRow& row : rows) bytes += row.name.size() + row.author.size() + row.version.size();
    return bytes;
}

}  // namespace reboot::browser
