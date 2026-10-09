#include "reboot/front/peer_user.hpp"

namespace rb::front {

PeerUser classify_peer(const Result<std::optional<u32>>& peer_uid, std::optional<u32> engine_uid) noexcept {
    if (!peer_uid) {
        const Diagnostic& error = peer_uid.error();
        const bool unsupported = error.domain == ErrorDomain::Platform && error.kind == ErrorKind::Unsupported;
        return unsupported ? PeerUser::Unchecked : PeerUser::Other;
    }
    if (!*peer_uid || !engine_uid) return PeerUser::Other;
    return **peer_uid == *engine_uid ? PeerUser::Engine : PeerUser::Other;
}

}  // namespace rb::front
