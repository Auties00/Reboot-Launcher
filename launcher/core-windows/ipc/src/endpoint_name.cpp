#include <string>
#include <string_view>

#include "reboot/ports/ipc.hpp"

namespace rb::ports {
namespace {

constexpr std::string_view kPipeNamePrefix = R"(\\.\pipe\reboot-engine-)";

}  // namespace

// ipc::endpoint_for checks both inputs before calling this.
std::string endpoint_name(const PeerIdentity& self, std::string_view root_hash16) {
    std::string name(kPipeNamePrefix);
    name += self.user_id;
    name += '-';
    name += root_hash16;
    return name;
}

}  // namespace rb::ports
