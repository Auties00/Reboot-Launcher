#pragma once

#include <algorithm>
#include <cstddef>
#include <span>
#include <vector>

#include "reboot/foundation/framing.hpp"
#include "reboot/foundation/types.hpp"
#include "reboot/testing/frame_log.hpp"
#include "reboot/testing/fuzz.hpp"

namespace rb::testing {

// Frames `input` whole and again in chunks sized by its first byte; both must yield the same
// frame types and status, and every frame is decoded as whichever of `Messages` owns its type.
template <ContractMessage... Messages>
void fuzz_frames(std::span<const u8> input, std::size_t max_frame) {
    const auto collect = [&](std::size_t chunk, std::vector<u64>& types) {
        Framer framer(max_frame);
        Framer::Status status{};
        for (std::size_t at = 0; at < input.size(); at += chunk) {
            status = framer.feed(input.subspan(at, std::min(chunk, input.size() - at)), [&](const RawFrame& frame) {
                types.push_back(frame.type);
                (void)decode_any_of<Messages...>(frame);
                return true;
            });
        }
        return status;
    };
    std::vector<u64> whole;
    std::vector<u64> chunked;
    const auto whole_status = collect(input.empty() ? 1 : input.size(), whole);
    const auto chunked_status = collect(input.empty() ? 1 : std::size_t{input[0]} % 7 + 1, chunked);
    fuzz_require(whole == chunked, "framing depends on how the input is chunked");
    fuzz_require(whole_status == chunked_status, "framing status depends on chunking");
}

}  // namespace rb::testing
