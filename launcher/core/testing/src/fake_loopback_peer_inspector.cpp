#include "reboot/testing/fake_loopback_peer_inspector.hpp"

#include <mutex>
#include <optional>

#include "messages.hpp"

namespace reboot::testing {

Result<std::optional<u32>> FakeLoopbackPeerInspector::peer_uid(Endpoint local, Endpoint remote) {
    const std::scoped_lock lock(mutex_);
    if (!supported_)
        return make_diag(ErrorDomain::Platform, msg::kNotSupported)
            .arg("feature", "loopback_peer_uid")
            .kind(ErrorKind::Unsupported)
            .fail();
    const auto it = uids_.find({local, remote});
    if (it == uids_.end()) return std::optional<u32>();
    return std::optional<u32>(it->second);
}

void FakeLoopbackPeerInspector::set_peer_uid(Endpoint local, Endpoint remote, u32 uid) {
    const std::scoped_lock lock(mutex_);
    uids_.insert_or_assign({local, remote}, uid);
}

void FakeLoopbackPeerInspector::set_supported(bool supported) {
    const std::scoped_lock lock(mutex_);
    supported_ = supported;
}

}  // namespace reboot::testing
