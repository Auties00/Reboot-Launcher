#include "reboot/gameserver/game_server_description.hpp"

#include "reboot/foundation/framing.hpp"

namespace reboot::gameserver {

bool same_description(const GameServerDescription& a, const GameServerDescription& b) {
    return encode_contract_frame(a) == encode_contract_frame(b);
}

}  // namespace reboot::gameserver
