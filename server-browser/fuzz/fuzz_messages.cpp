// Every message decoder must reject or accept arbitrary bytes without UB, and anything it
// accepts must re-encode canonically (decode(encode(x)) == x at the byte level).

#include <cstdlib>

#include "wire/frame.hpp"
#include "wire/messages.hpp"

using namespace sb;
using namespace sb::wire;

namespace {

template <class T>
void roundtrip(std::span<const u8> in) {
    T a{};
    if (!decode(in, a)) return;
    const Bytes once = encode_to_bytes(a);
    T b{};
    if (!decode(once, b)) std::abort();
    if (encode_to_bytes(b) != once) std::abort();
}

}  // namespace

extern "C" int LLVMFuzzerTestOneInput(const u8* data, std::size_t size) {
    if (size == 0) return 0;
    const std::span<const u8> in(data + 1, size - 1);
    switch (data[0] % 22) {
        case 0: roundtrip<Hello>(in); break;
        case 1: roundtrip<Welcome>(in); break;
        case 2: roundtrip<Subscribe>(in); break;
        case 3: roundtrip<SubOpen>(in); break;
        case 4: roundtrip<Unsubscribe>(in); break;
        case 5: roundtrip<Patch>(in); break;
        case 6: roundtrip<Delta>(in); break;
        case 7: roundtrip<Snapshot>(in); break;
        case 8: roundtrip<Query>(in); break;
        case 9: roundtrip<QueryResult>(in); break;
        case 10: roundtrip<Resolve>(in); break;
        case 11: roundtrip<ResolveResult>(in); break;
        case 12: roundtrip<Join>(in); break;
        case 13: roundtrip<JoinGrant>(in); break;
        case 14: roundtrip<HostRegister>(in); break;
        case 15: roundtrip<HostRegistered>(in); break;
        case 16: roundtrip<HostUpdate>(in); break;
        case 17: roundtrip<HostUnregister>(in); break;
        case 18: roundtrip<HostHeartbeat>(in); break;
        case 19: roundtrip<Error>(in); break;
        case 20: roundtrip<GoAway>(in); break;
        case 21: roundtrip<ListEntry>(in); break;
    }
    return 0;
}
