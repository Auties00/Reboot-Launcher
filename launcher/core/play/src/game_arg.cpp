#include "reboot/play/game_arg.hpp"

namespace reboot::play {

std::string GameArg::text() const {
    if (!value || value->empty()) return key;
    std::string out;
    out.reserve(key.size() + 1 + value->size());
    out.append(key).append(1, '=').append(*value);
    return out;
}

}  // namespace reboot::play
