#include "json_text.hpp"

#include <array>
#include <cstddef>
#include <exception>

#include <boost/json/parse.hpp>
#include <boost/json/serialize.hpp>

namespace reboot::storage {

namespace json = boost::json;

namespace {

constexpr std::string_view kBase64Alphabet = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

void indent(std::string& out, std::size_t depth) { out.append(depth * 2, ' '); }

void write_pretty(std::string& out, const json::value& value, std::size_t depth) {
    if (const json::object* object = value.if_object()) {
        if (object->empty()) {
            out += "{}";
            return;
        }
        out += "{\n";
        std::size_t index = 0;
        for (const json::key_value_pair& member : *object) {
            indent(out, depth + 1);
            out += json::serialize(json::value(member.key()));
            out += ": ";
            write_pretty(out, member.value(), depth + 1);
            out += ++index < object->size() ? ",\n" : "\n";
        }
        indent(out, depth);
        out += '}';
        return;
    }
    if (const json::array* array = value.if_array()) {
        if (array->empty()) {
            out += "[]";
            return;
        }
        out += "[\n";
        std::size_t index = 0;
        for (const json::value& element : *array) {
            indent(out, depth + 1);
            write_pretty(out, element, depth + 1);
            out += ++index < array->size() ? ",\n" : "\n";
        }
        indent(out, depth);
        out += ']';
        return;
    }
    out += json::serialize(value);
}

[[nodiscard]] constexpr int base64_digit(char c) noexcept {
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= 'a' && c <= 'z') return c - 'a' + 26;
    if (c >= '0' && c <= '9') return c - '0' + 52;
    if (c == '+') return 62;
    if (c == '/') return 63;
    return -1;
}

}  // namespace

std::string to_pretty_json(const json::value& value) {
    std::string out;
    write_pretty(out, value, 0);
    out += '\n';
    return out;
}

std::optional<json::value> parse_json(std::string_view text) {
    try {
        boost::system::error_code error;
        json::value value = json::parse(json::string_view(text.data(), text.size()), error);
        if (error) return std::nullopt;
        return value;
    } catch (const std::exception&) {
        return std::nullopt;
    }
}

std::string base64_encode(std::span<const u8> bytes) {
    std::string out;
    out.reserve((bytes.size() + 2) / 3 * 4);
    std::size_t i = 0;
    for (; i + 3 <= bytes.size(); i += 3) {
        const u32 group = (u32{bytes[i]} << 16) | (u32{bytes[i + 1]} << 8) | u32{bytes[i + 2]};
        out += kBase64Alphabet[(group >> 18) & 63];
        out += kBase64Alphabet[(group >> 12) & 63];
        out += kBase64Alphabet[(group >> 6) & 63];
        out += kBase64Alphabet[group & 63];
    }
    const std::size_t rest = bytes.size() - i;
    if (rest == 0) return out;
    const u32 group = (u32{bytes[i]} << 16) | (rest == 2 ? u32{bytes[i + 1]} << 8 : 0u);
    out += kBase64Alphabet[(group >> 18) & 63];
    out += kBase64Alphabet[(group >> 12) & 63];
    out += rest == 2 ? kBase64Alphabet[(group >> 6) & 63] : '=';
    out += '=';
    return out;
}

std::optional<std::vector<u8>> base64_decode(std::string_view text) {
    if (text.size() % 4 != 0) return std::nullopt;
    std::vector<u8> out;
    out.reserve(text.size() / 4 * 3);
    for (std::size_t i = 0; i < text.size(); i += 4) {
        const bool last = i + 4 == text.size();
        std::array<int, 4> digits{};
        std::size_t padding = 0;
        for (std::size_t j = 0; j < 4; ++j) {
            const char c = text[i + j];
            if (c == '=' && last && j >= 2) {
                ++padding;
                digits[j] = 0;
                continue;
            }
            if (padding > 0) return std::nullopt;
            digits[j] = base64_digit(c);
            if (digits[j] < 0) return std::nullopt;
        }
        const u32 group = (static_cast<u32>(digits[0]) << 18) | (static_cast<u32>(digits[1]) << 12) |
                          (static_cast<u32>(digits[2]) << 6) | static_cast<u32>(digits[3]);
        out.push_back(static_cast<u8>(group >> 16));
        if (padding < 2) out.push_back(static_cast<u8>(group >> 8));
        if (padding < 1) out.push_back(static_cast<u8>(group));
    }
    return out;
}

}  // namespace reboot::storage
