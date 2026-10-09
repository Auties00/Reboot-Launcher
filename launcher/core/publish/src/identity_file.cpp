#include "identity_file.hpp"

#include <array>
#include <string_view>
#include <utility>
#include <vector>

#include <boost/json/object.hpp>
#include <boost/json/parse.hpp>
#include <boost/json/string.hpp>
#include <boost/json/value.hpp>

#include "reboot/foundation/diag.hpp"

namespace rb::publish {

namespace json = boost::json;

namespace {

constexpr std::string_view kServerIdKey = "server_id";
constexpr std::string_view kTokenKey = "token";
constexpr char kHexDigits[] = "0123456789abcdef";

[[nodiscard]] int hex_value(char c) noexcept {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

void append_hex(std::string& out, std::span<const u8> bytes) {
    for (const u8 byte : bytes) {
        out.push_back(kHexDigits[byte >> 4]);
        out.push_back(kHexDigits[byte & 0xF]);
    }
}

[[nodiscard]] std::optional<HostToken> parse_token(std::string_view text) {
    if (text.size() != kHostTokenSize * 2) return std::nullopt;
    std::array<u8, kHostTokenSize> bytes{};
    for (std::size_t i = 0; i < kHostTokenSize; ++i) {
        const int high = hex_value(text[2 * i]);
        const int low = hex_value(text[(2 * i) + 1]);
        if (high < 0 || low < 0) {
            secure_wipe(bytes.data(), bytes.size());
            return std::nullopt;
        }
        bytes[i] = static_cast<u8>((high << 4) | low);
    }
    HostToken token(bytes);
    secure_wipe(bytes.data(), bytes.size());
    return token;
}

void wipe(json::value& value) noexcept {
    if (json::string* text = value.if_string()) secure_wipe(text->data(), text->size());
}

}  // namespace

SecretBytes encode_identity(const ServerId& server, const std::optional<HostToken>& token) {
    std::string text;
    // Sized up front so the token is never left behind in a freed buffer.
    text.reserve(128);
    text += "{\n  \"";
    text += kServerIdKey;
    text += "\": \"";
    text += format_uuid(server.value);
    text += '"';
    if (token) {
        text += ",\n  \"";
        text += kTokenKey;
        text += "\": \"";
        append_hex(text, token->reveal());
        text += '"';
    }
    text += "\n}\n";
    std::vector<u8> bytes(text.begin(), text.end());
    secure_wipe(text.data(), text.size());
    return SecretBytes(std::move(bytes));
}

std::optional<StoredIdentity> decode_identity(std::span<const u8> bytes) {
    std::string_view text(reinterpret_cast<const char*>(bytes.data()), bytes.size());
    // Some editors save a hand edit with a UTF-8 byte order mark.
    if (text.starts_with("\xEF\xBB\xBF")) text.remove_prefix(3);
    boost::system::error_code error;
    json::value parsed = json::parse(text, error);
    if (error || !parsed.is_object()) return std::nullopt;
    json::object& object = parsed.as_object();

    std::optional<StoredIdentity> out;
    const json::value* server = object.if_contains(kServerIdKey);
    json::value* token = object.if_contains(kTokenKey);
    if (server != nullptr && server->is_string()) {
        const json::string& server_text = server->get_string();
        const Result<Uuid> uuid = parse_uuid(std::string_view(server_text.data(), server_text.size()));
        if (uuid && !uuid->is_nil()) {
            out.emplace(StoredIdentity{ServerId{*uuid}, std::nullopt});
            if (token != nullptr) {
                const json::string* token_string = token->if_string();
                if (token_string != nullptr) out->token = parse_token(std::string_view(token_string->data(), token_string->size()));
                if (!out->token) out.reset();
            }
        }
    }
    if (token != nullptr) wipe(*token);
    return out;
}

SecretString token_text(const HostToken& token) {
    std::string text;
    text.reserve(kHostTokenSize * 2);
    append_hex(text, token.reveal());
    return SecretString(std::move(text));
}

}  // namespace rb::publish
