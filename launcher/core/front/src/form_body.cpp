#include "form_body.hpp"

#include "reboot/foundation/secret.hpp"

namespace reboot::front {

namespace {

[[nodiscard]] int hex_digit(char c) noexcept {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

[[nodiscard]] bool decode(std::string_view text, std::string& out) {
    out.clear();
    out.reserve(text.size());
    for (std::size_t i = 0; i < text.size(); ++i) {
        const char c = text[i];
        if (c == '+') {
            out.push_back(' ');
        } else if (c == '%') {
            if (i + 2 >= text.size()) return false;
            const int high = hex_digit(text[i + 1]);
            const int low = hex_digit(text[i + 2]);
            if (high < 0 || low < 0) return false;
            out.push_back(static_cast<char>(high << 4 | low));
            i += 2;
        } else {
            out.push_back(c);
        }
    }
    return true;
}

void encode(std::vector<u8>& out, std::string_view text) {
    constexpr std::string_view kHex = "0123456789ABCDEF";
    for (const char c : text) {
        const auto byte = static_cast<u8>(c);
        const bool unreserved = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') ||
                                c == '-' || c == '.' || c == '_' || c == '~';
        if (unreserved) {
            out.push_back(byte);
        } else if (c == ' ') {
            out.push_back('+');
        } else {
            out.push_back('%');
            out.push_back(static_cast<u8>(kHex[byte >> 4]));
            out.push_back(static_cast<u8>(kHex[byte & 0x0F]));
        }
    }
}

void wipe(std::string& text) noexcept {
    text.resize(text.capacity());
    secure_wipe(text.data(), text.size());
    text.clear();
}

}  // namespace

FormFields::~FormFields() {
    for (FormField& field : fields) {
        wipe(field.name);
        wipe(field.value);
    }
}

bool parse_form(std::span<const u8> body, FormFields& out) {
    std::string_view text(reinterpret_cast<const char*>(body.data()), body.size());
    while (!text.empty()) {
        const std::size_t amp = text.find('&');
        const std::string_view pair = text.substr(0, amp);
        text = amp == std::string_view::npos ? std::string_view{} : text.substr(amp + 1);
        if (pair.empty()) continue;
        const std::size_t eq = pair.find('=');
        FormField& field = out.fields.emplace_back();
        if (!decode(pair.substr(0, eq), field.name)) return false;
        if (eq != std::string_view::npos && !decode(pair.substr(eq + 1), field.value)) return false;
    }
    return true;
}

void append_form_field(std::vector<u8>& out, std::string_view name, std::string_view value) {
    if (!out.empty()) out.push_back('&');
    encode(out, name);
    out.push_back('=');
    encode(out, value);
}

}  // namespace reboot::front
