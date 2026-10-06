// Stream reassembly must produce the same frames no matter how the bytes are split.

#include <cstdlib>
#include <vector>

#include "wire/frame.hpp"

using namespace sb;
using namespace sb::wire;

extern "C" int LLVMFuzzerTestOneInput(const u8* data, std::size_t size) {
    if (size < 2) return 0;
    const std::size_t chunk = data[0] % 17 + 1;
    const std::span<const u8> in(data + 1, size - 1);

    std::vector<std::pair<u64, std::vector<u8>>> whole, split;
    StreamFramer a(1024);
    const auto sa = a.feed(in, [&](const FrameView& f) {
        whole.emplace_back(static_cast<u64>(f.type), std::vector<u8>(f.payload.begin(), f.payload.end()));
        return true;
    });

    StreamFramer b(1024);
    auto sb = StreamFramer::Status::ok;
    for (std::size_t off = 0; off < in.size() && sb == StreamFramer::Status::ok; off += chunk) {
        const std::size_t n = std::min(chunk, in.size() - off);
        sb = b.feed(in.subspan(off, n), [&](const FrameView& f) {
            split.emplace_back(static_cast<u64>(f.type), std::vector<u8>(f.payload.begin(), f.payload.end()));
            return true;
        });
    }
    if (sa == StreamFramer::Status::ok && sb == StreamFramer::Status::ok && whole != split) std::abort();
    // Datagram parsing of the same bytes must never crash either.
    (void)for_each_frame(in, 1500, [](const FrameView&) { return true; });
    return 0;
}
