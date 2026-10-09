#include "reboot/gameserver/game_server_description.hpp"

#include "reboot/foundation/framing.hpp"

namespace rb::gameserver {

bool same_description(const GameServerDescription& a, const GameServerDescription& b) {
    return encode_contract_frame(a) == encode_contract_frame(b);
}

}  // namespace rb::gameserver
