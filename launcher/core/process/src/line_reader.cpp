#include "reboot/process/line_reader.hpp"

#include <utility>

namespace reboot::process {

namespace {

constexpr std::string_view kReplacement = "\xEF\xBF\xBD";

// Bytes after the lead byte, or 0 for a byte that cannot start a sequence.
[[nodiscard]] u8 continuation_count(u8 lead) {
    if (lead >= 0xC2 && lead <= 0xDF) return 1;
    if (lead >= 0xE0 && lead <= 0xEF) return 2;
    if (lead >= 0xF0 && lead <= 0xF4) return 3;
    return 0;
}

// The second byte's range rules out overlongs, surrogates and code points above U+10FFFF.
[[nodiscard]] bool valid_continuation(u8 lead, u8 index, u8 byte) {
    if (index == 1) {
        if (lead == 0xE0) return byte >= 0xA0 && byte <= 0xBF;
        if (lead == 0xED) return byte >= 0x80 && byte <= 0x9F;
        if (lead == 0xF0) return byte >= 0x90 && byte <= 0xBF;
        if (lead == 0xF4) return byte >= 0x80 && byte <= 0x8F;
    }
    return byte >= 0x80 && byte <= 0xBF;
}

}  // namespace

LineReader::LineReader(LineCallback on_line) : on_line_(std::move(on_line)) {}

void LineReader::append(std::string_view code_point) {
    // Cut only when more follows, so a continued line is never the last one emitted.
    if (line_.size() + code_point.size() > kMaxLineBytes) {
        on_line_(line_, true);
        line_.clear();
    }
    line_.append(code_point);
}

void LineReader::end_line() {
    on_line_(line_, false);
    line_.clear();
}

void LineReader::flush_partial() {
    if (partial_size_ == 0) return;
    partial_size_ = 0;
    append(kReplacement);
}

void LineReader::feed(std::span<const u8> bytes) {
    for (const u8 byte : bytes) {
        if (partial_size_ > 0) {
            const u8 lead = partial_[0];
            if (valid_continuation(lead, partial_size_, byte)) {
                partial_[partial_size_++] = byte;
                if (partial_size_ == continuation_count(lead) + 1) {
                    append(std::string_view(reinterpret_cast<const char*>(partial_.data()), partial_size_));
                    partial_size_ = 0;
                }
                continue;
            }
            flush_partial();
        }

        const bool crlf_tail = after_cr_ && byte == '\n';
        after_cr_ = false;
        if (crlf_tail) continue;
        if (byte == '\r' || byte == '\n') {
            after_cr_ = byte == '\r';
            end_line();
            continue;
        }
        if (byte < 0x80) {
            const char c = static_cast<char>(byte);
            append(std::string_view(&c, 1));
            continue;
        }
        if (continuation_count(byte) == 0) {
            append(kReplacement);
            continue;
        }
        partial_[0] = byte;
        partial_size_ = 1;
    }
}

void LineReader::finish() {
    flush_partial();
    if (!line_.empty()) end_line();
    after_cr_ = false;
}

}  // namespace reboot::process
