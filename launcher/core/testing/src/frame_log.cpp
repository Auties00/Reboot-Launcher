#include "reboot/testing/frame_log.hpp"

#include <algorithm>
#include <cstddef>
#include <span>

#include "reboot/foundation/diag.hpp"
#include "reboot/foundation/framing.hpp"

namespace rb::testing {

Result<void> FrameLog::feed(std::span<const u8> bytes) {
    if (malformed_) return make_diag(ErrorDomain::Contracts, kMalformedFrame).fail();
    const Framer::Status status = framer_.feed(bytes, [this](const RawFrame& frame) {
        frames_.push_back({frame.type, {frame.payload.begin(), frame.payload.end()}});
        return true;
    });
    if (status == Framer::Status::ok) return {};
    malformed_ = true;
    return make_diag(ErrorDomain::Contracts, kMalformedFrame).fail();
}

std::size_t FrameLog::count(u64 type) const {
    return static_cast<std::size_t>(std::ranges::count(frames_, type, &OwnedFrame::type));
}

}  // namespace rb::testing
