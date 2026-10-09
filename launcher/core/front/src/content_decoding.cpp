#include "content_decoding.hpp"

#include <algorithm>

#include <boost/beast/zlib/error.hpp>
#include <boost/beast/zlib/inflate_stream.hpp>

#include "reboot/foundation/text.hpp"

namespace reboot::front {

namespace {

namespace zlib = boost::beast::zlib;

constexpr std::size_t kOutputStep = 64 * 1024;

[[nodiscard]] std::string_view trim(std::string_view text) noexcept {
    const std::size_t first = text.find_first_not_of(" \t");
    if (first == std::string_view::npos) return {};
    return text.substr(first, text.find_last_not_of(" \t") - first + 1);
}

// Raw deflate; the gzip and zlib trailers are not checked, since nothing is forwarded from here.
[[nodiscard]] std::optional<std::vector<u8>> inflate_raw(std::span<const u8> input, std::size_t cap) {
    zlib::inflate_stream stream;
    zlib::z_params params{};
    params.next_in = input.data();
    params.avail_in = input.size();
    std::vector<u8> out;
    for (;;) {
        const std::size_t used = out.size();
        if (used > cap) return std::nullopt;
        out.resize(std::min(used + kOutputStep, cap + 1));
        params.next_out = out.data() + used;
        params.avail_out = out.size() - used;
        boost::system::error_code error;
        stream.write(params, zlib::Flush::sync, error);
        out.resize(out.size() - params.avail_out);
        if (error == zlib::error::end_of_stream) {
            if (out.size() > cap) return std::nullopt;
            return out;
        }
        if (error == zlib::error::need_buffers) {
            // No progress with output room left: the input ended before the stream did.
            if (params.avail_out != 0 || params.avail_in == 0) return std::nullopt;
            continue;
        }
        if (error) return std::nullopt;
    }
}

[[nodiscard]] std::optional<std::vector<u8>> gunzip(std::span<const u8> body, std::size_t cap) {
    constexpr u8 kHeaderCrc = 0x02;
    constexpr u8 kExtra = 0x04;
    constexpr u8 kName = 0x08;
    constexpr u8 kComment = 0x10;
    if (body.size() < 18 || body[0] != 0x1F || body[1] != 0x8B || body[2] != 8) return std::nullopt;
    const u8 flags = body[3];
    std::size_t at = 10;
    if (flags & kExtra) {
        if (at + 2 > body.size()) return std::nullopt;
        at += 2 + (static_cast<std::size_t>(body[at]) | static_cast<std::size_t>(body[at + 1]) << 8);
    }
    for (const u8 field : {kName, kComment}) {
        if (!(flags & field)) continue;
        while (at < body.size() && body[at] != 0) ++at;
        ++at;
    }
    if (flags & kHeaderCrc) at += 2;
    if (at >= body.size()) return std::nullopt;
    return inflate_raw(body.subspan(at), cap);
}

// "deflate" is the zlib format, though some servers send raw deflate under that name.
[[nodiscard]] std::optional<std::vector<u8>> inflate_any(std::span<const u8> body, std::size_t cap) {
    const bool zlib_header = body.size() >= 2 && (body[0] & 0x0F) == 8 && (body[1] & 0x20) == 0 &&
                             ((static_cast<unsigned>(body[0]) << 8) | body[1]) % 31 == 0;
    return inflate_raw(zlib_header ? body.subspan(2) : body, cap);
}

}  // namespace

std::optional<std::vector<u8>> decode_content(std::string_view content_encoding, std::span<const u8> body,
                                              std::size_t cap) {
    const std::string_view coding = trim(content_encoding);
    if (coding.empty() || iequals_ascii(coding, "identity")) {
        if (body.size() > cap) return std::nullopt;
        return std::vector<u8>(body.begin(), body.end());
    }
    if (iequals_ascii(coding, "gzip") || iequals_ascii(coding, "x-gzip")) return gunzip(body, cap);
    if (iequals_ascii(coding, "deflate")) return inflate_any(body, cap);
    return std::nullopt;
}

}  // namespace reboot::front
