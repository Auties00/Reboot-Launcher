// A client mirror fed arbitrary snapshots and patches must stay internally consistent.

#include <cstdlib>

#include "client/view_mirror.hpp"
#include "wire/frame.hpp"

using namespace sb;
using namespace sb::wire;

extern "C" int LLVMFuzzerTestOneInput(const u8* data, std::size_t size) {
    client::ViewMirror m(64);
    (void)for_each_frame(std::span<const u8>(data, size), 1 << 16, [&](const FrameView& f) {
        if (f.type == FrameType::snapshot) {
            Snapshot s;
            if (decode_frame(f, s)) m.on_snapshot(s);
        } else if (f.type == FrameType::delta) {
            Delta d;
            if (decode_frame(f, d)) m.on_delta(d);
        }
        return true;
    });
    for (auto sort : {Sort::players, Sort::newest, Sort::name}) {
        const auto v = m.sorted(sort);
        if (v.size() != m.size()) std::abort();
        for (std::size_t i = 1; i < v.size(); ++i)
            if (client::ViewMirror::less(sort, v[i], v[i - 1])) std::abort();
    }
    return 0;
}
