#pragma once

#include <array>
#include <cstddef>
#include <span>
#include <string>
#include <string_view>

#include "reboot/foundation/function.hpp"
#include "reboot/foundation/types.hpp"

namespace reboot::process {

// Capabilities: game-launch.output-line-framing.
// Turns a child's raw pipe chunks into whole lines, for every child's stdout and stderr. LF, CR
// and CRLF each end one line, even when a CRLF straddles two chunks; the terminator is not part of
// the line. UTF-8 is decoded across chunk boundaries and each invalid sequence becomes U+FFFD. A
// line longer than kMaxLineBytes is cut on a code point boundary. Not thread-safe; one per stream.
class LineReader {
public:
    static constexpr std::size_t kMaxLineBytes = std::size_t{64} << 10;

    // `continued` is true when the line was cut at kMaxLineBytes and the next call carries the rest.
    using LineCallback = UniqueFunction<void(std::string_view line, bool continued)>;

    explicit LineReader(LineCallback on_line);

    void feed(std::span<const u8> bytes);
    // End of stream: emits the unterminated tail, if any, and resets the reader. Never emits an
    // empty line for a stream that ended with its terminator.
    void finish();

private:
    void append(std::string_view code_point);
    void end_line();
    // An unfinished UTF-8 sequence becomes one U+FFFD.
    void flush_partial();

    LineCallback on_line_;
    std::string line_;
    std::array<u8, 4> partial_{};
    u8 partial_size_ = 0;
    bool after_cr_ = false;
};

}  // namespace reboot::process
