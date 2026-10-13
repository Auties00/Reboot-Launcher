#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "reboot/api/codec.hpp"
#include "reboot/api/v1/common.hpp"
#include "reboot/client.h"
#include "reboot/sdk/buffer.hpp"
#include "reboot/sdk/english_catalog.hpp"

namespace rb::sdk {

using ErrorArg = api::DiagnosticArg;

namespace detail {

// The SDK's own errors, which no engine catalog holds.
inline constexpr std::array<EnglishTemplate, 6> kSdkEnglish{{
    {"sdk.abi_mismatch", "This program needs reboot_client ABI {expected}, but the library is ABI {library}."},
    {"sdk.cancelled", "The operation was cancelled."},
    {"sdk.malformed_reply", "The engine sent a {what} this program could not read."},
    {"sdk.no_diagnostic", "reboot_client failed with {status}."},
    {"sdk.schema_mismatch", "This program was built for another version of the engine (schema {client}, engine {engine})."},
    {"sdk.timed_out", "The operation ran out of time ({phase})."},
}};

[[nodiscard]] inline std::optional<std::string_view> english_template(std::string_view id) noexcept {
    for (const EnglishTemplate& entry : kSdkEnglish)
        if (entry.id == id) return entry.text;
    const auto it = std::ranges::lower_bound(kEnglish, id, {}, &EnglishTemplate::id);
    if (it != kEnglish.end() && it->id == id) return it->text;
    return std::nullopt;
}

[[nodiscard]] inline std::string arg_text(const ErrorArg& arg) {
    switch (arg.kind) {
        case api::ArgKind::Millis: return arg.value + " ms";
        default: return arg.value;
    }
}

[[nodiscard]] inline const ErrorArg* find_arg(const std::vector<ErrorArg>& args, std::string_view name) noexcept {
    for (const ErrorArg& arg : args)
        if (arg.name == name) return &arg;
    return nullptr;
}

[[nodiscard]] inline std::string_view trim(std::string_view text) noexcept {
    while (!text.empty() && text.front() == ' ') text.remove_prefix(1);
    while (!text.empty() && text.back() == ' ') text.remove_suffix(1);
    return text;
}

// The end of the {...} that opens at `open`, or npos.
[[nodiscard]] inline std::size_t closing_brace(std::string_view text, std::size_t open) noexcept {
    int depth = 0;
    for (std::size_t i = open; i < text.size(); ++i) {
        if (text[i] == '{') ++depth;
        if (text[i] == '}' && --depth == 0) return i;
    }
    return std::string_view::npos;
}

inline std::string render_template(std::string_view text, const std::vector<ErrorArg>& args, const ErrorArg* plural);

// "{n, plural, one {...} other {...}}" in English: `one` only for exactly 1.
[[nodiscard]] inline std::string render_plural(std::string_view cases, const ErrorArg& arg,
                                               const std::vector<ErrorArg>& args) {
    const std::string_view wanted = arg.value == "1" ? "one" : "other";
    std::string_view fallback;
    for (std::size_t i = 0; i < cases.size();) {
        const std::size_t open = cases.find('{', i);
        if (open == std::string_view::npos) break;
        const std::size_t close = closing_brace(cases, open);
        if (close == std::string_view::npos) break;
        const std::string_view selector = trim(cases.substr(i, open - i));
        const std::string_view body = cases.substr(open + 1, close - open - 1);
        if (selector == wanted) return render_template(body, args, &arg);
        if (selector == "other") fallback = body;
        i = close + 1;
    }
    return render_template(fallback, args, &arg);
}

// ICU MessageFormat as the engine's catalog uses it: {name}, English one/other plurals with '#',
// and ICU's default apostrophe quoting. Anything else is copied as written.
inline std::string render_template(std::string_view text, const std::vector<ErrorArg>& args, const ErrorArg* plural) {
    std::string out;
    out.reserve(text.size());
    for (std::size_t i = 0; i < text.size();) {
        const char c = text[i];
        if (c == '\'' && i + 1 < text.size() && text[i + 1] == '\'') {
            out += '\'';
            i += 2;
        } else if (c == '\'' && i + 1 < text.size() && (text[i + 1] == '{' || text[i + 1] == '}' || text[i + 1] == '#')) {
            std::size_t end = i + 1;
            while (end < text.size()) {
                if (text[end] == '\'' && end + 1 < text.size() && text[end + 1] == '\'') {
                    out += '\'';
                    end += 2;
                } else if (text[end] == '\'') {
                    break;
                } else {
                    out += text[end++];
                }
            }
            i = end + 1;
        } else if (c == '#' && plural != nullptr) {
            out += plural->value;
            ++i;
        } else if (c == '{') {
            const std::size_t close = closing_brace(text, i);
            if (close == std::string_view::npos) {
                out.append(text.substr(i));
                break;
            }
            const std::string_view inner = text.substr(i + 1, close - i - 1);
            const std::size_t comma = inner.find(',');
            const ErrorArg* arg = find_arg(args, trim(inner.substr(0, comma)));
            if (arg == nullptr) {
                out.append(text.substr(i, close - i + 1));
            } else if (comma == std::string_view::npos) {
                out += arg_text(*arg);
            } else {
                const std::string_view rest = inner.substr(comma + 1);
                const std::size_t second = rest.find(',');
                if (second != std::string_view::npos && trim(rest.substr(0, second)) == "plural")
                    out += render_plural(rest.substr(second + 1), *arg, args);
                else
                    out += arg_text(*arg);
            }
            i = close + 1;
        } else {
            out += c;
            ++i;
        }
    }
    return out;
}

}  // namespace detail

// A failure: the engine's Diagnostic, reboot_client's own, or the SDK's (ids sdk.*), with the
// rb_status of the call that failed.
class Error {
public:
    Error(rb_status status, api::Diagnostic diagnostic) : status_(status), diagnostic_(std::move(diagnostic)) {}

    // The Diagnostic rb_last_error holds for this thread's last failed call.
    [[nodiscard]] static Error last(rb_status status) {
        Buffer buffer;
        if (rb_last_error(buffer.out()) != RB_OK || buffer.empty())
            return local(status, "sdk.no_diagnostic", {{"status", api::ArgKind::String, rb_status_name(status)}});
        return decode(status, buffer.bytes());
    }

    [[nodiscard]] static Error decode(rb_status status, std::span<const std::uint8_t> diagnostic) {
        std::expected<api::Diagnostic, api::DecodeError> decoded = api::decode<api::Diagnostic>(diagnostic);
        if (!decoded) return local(status, "sdk.malformed_reply", {{"what", api::ArgKind::String, "diagnostic"}});
        return {status, std::move(*decoded)};
    }

    [[nodiscard]] static Error local(rb_status status, std::string id, std::vector<ErrorArg> args = {},
                                     api::ErrorKind kind = api::ErrorKind::Generic) {
        api::Diagnostic diagnostic;
        diagnostic.id = std::move(id);
        diagnostic.args = std::move(args);
        diagnostic.kind = kind;
        return {status, std::move(diagnostic)};
    }

    [[nodiscard]] rb_status status() const noexcept { return status_; }
    [[nodiscard]] const std::string& id() const noexcept { return diagnostic_.id; }
    [[nodiscard]] const std::vector<ErrorArg>& args() const noexcept { return diagnostic_.args; }
    [[nodiscard]] const std::string* arg(std::string_view name) const noexcept {
        const ErrorArg* found = detail::find_arg(diagnostic_.args, name);
        return found == nullptr ? nullptr : &found->value;
    }
    // Literal text from the OS or a peer; never translated.
    [[nodiscard]] const std::optional<std::string>& detail() const noexcept { return diagnostic_.detail; }
    [[nodiscard]] api::ErrorKind kind() const noexcept { return diagnostic_.kind; }
    [[nodiscard]] bool retryable() const noexcept { return diagnostic_.retryable; }
    [[nodiscard]] api::OsErrorOrigin os_origin() const noexcept { return diagnostic_.os_origin; }
    [[nodiscard]] std::optional<std::int64_t> os_code() const noexcept { return diagnostic_.os_code; }
    [[nodiscard]] const api::Diagnostic& diagnostic() const noexcept { return diagnostic_; }

    // The id's English template with its args, then the detail in brackets; the id when no template
    // is known.
    [[nodiscard]] std::string english() const {
        const std::optional<std::string_view> text = detail::english_template(diagnostic_.id);
        std::string out = text ? detail::render_template(*text, diagnostic_.args, nullptr) : diagnostic_.id;
        if (diagnostic_.detail && !diagnostic_.detail->empty()) out += " (" + *diagnostic_.detail + ")";
        return out;
    }

private:
    rb_status status_;
    api::Diagnostic diagnostic_;
};

template <class T>
using Expected = std::expected<T, Error>;

}  // namespace rb::sdk
