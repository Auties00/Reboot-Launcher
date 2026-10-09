#include "reboot/play/launch_args.hpp"

#include <algorithm>
#include <array>
#include <utility>

#include "messages.hpp"
#include "reboot/foundation/text.hpp"
#include "reboot/storage/settings_values.hpp"
#include "wipe.hpp"

namespace rb::play {

namespace {

constexpr std::array<std::string_view, 3> kReservedKeys{kAuthLoginKey, kAuthPasswordKey, kAuthTypeKey};
constexpr std::string_view kEpicLocaleKey = "-epiclocale";

[[nodiscard]] constexpr bool is_space(char c) noexcept {
    return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\v' || c == '\f';
}

[[nodiscard]] bool reserved(std::string_view key) {
    return std::ranges::any_of(kReservedKeys, [key](std::string_view name) { return iequals_ascii(key, name); });
}

[[nodiscard]] std::unexpected<CustomArgsIssue> issue(CustomArgsError error, std::size_t offset, std::string key = {}) {
    return std::unexpected(CustomArgsIssue{error, offset, std::move(key)});
}

void push_or_replace(std::vector<GameArg>& args, GameArg arg) {
    const auto existing = std::ranges::find_if(args, [&](const GameArg& held) { return iequals_ascii(held.key, arg.key); });
    if (existing == args.end()) {
        args.push_back(std::move(arg));
        return;
    }
    if (existing->value) wipe(*existing->value);
    existing->value = std::move(arg.value);
    existing->secret = existing->secret || arg.secret;
}

[[nodiscard]] bool needs_quotes(std::string_view value) {
    return value.empty() || std::ranges::any_of(value, [](char c) { return is_space(c) || c == '"'; });
}

void append_quoted(std::string& out, std::string_view value) {
    out.push_back('"');
    for (const char c : value) {
        if (c == '"') out.push_back('\\');
        out.push_back(c);
    }
    out.push_back('"');
}

}  // namespace

Diagnostic to_diagnostic(const CustomArgsIssue& issue) {
    const auto at = [&](MessageId message) {
        return make_diag(ErrorDomain::Play, message)
            .kind(ErrorKind::InvalidInput)
            .detail("at byte " + std::to_string(issue.offset));
    };
    switch (issue.error) {
        case CustomArgsError::UnbalancedQuote: return at(msg::kCustomArgsUnbalancedQuote).build();
        case CustomArgsError::InvalidText: return at(msg::kCustomArgsInvalid).build();
        case CustomArgsError::ReservedKey: return at(msg::kCustomArgsReserved).arg("key", issue.key).build();
    }
    return internal_bug("play.to_diagnostic(CustomArgsIssue)");
}

std::expected<std::vector<GameArg>, CustomArgsIssue> parse_custom_args(std::string_view text) {
    std::vector<GameArg> out;
    std::size_t i = 0;
    while (i < text.size()) {
        if (is_space(text[i])) {
            ++i;
            continue;
        }
        const std::size_t start = i;
        std::string token;
        char quote = 0;
        while (i < text.size()) {
            const char c = text[i];
            if (quote == 0 && is_space(c)) break;
            // Only \" escapes, so a Windows path keeps its backslashes; single quotes take nothing literally.
            if (c == '\\' && quote != '\'' && i + 1 < text.size() && text[i + 1] == '"') {
                token.push_back('"');
                i += 2;
                continue;
            }
            if (quote == 0 && (c == '"' || c == '\'')) {
                quote = c;
            } else if (quote != 0 && c == quote) {
                quote = 0;
            } else {
                token.push_back(c);
            }
            ++i;
        }
        const std::string_view raw = text.substr(start, i - start);
        if (raw.find('\0') != std::string_view::npos || !is_valid_utf8(raw))
            return issue(CustomArgsError::InvalidText, start);
        if (quote != 0) return issue(CustomArgsError::UnbalancedQuote, start);
        if (token.empty()) continue;

        GameArg arg;
        if (const std::size_t eq = token.find('='); eq == std::string::npos) {
            arg.key = std::move(token);
        } else {
            arg.key = token.substr(0, eq);
            arg.value = token.substr(eq + 1);
        }
        if (reserved(arg.key)) return issue(CustomArgsError::ReservedKey, start, arg.key);
        out.push_back(std::move(arg));
    }
    return out;
}

void map_host_paths(std::vector<GameArg>& custom_args, const compat::PathMapper& paths) {
    for (GameArg& arg : custom_args) {
        if (!arg.value || arg.value->empty()) continue;
        const NativePath host(std::u8string(arg.value->begin(), arg.value->end()));
        if (!host.is_absolute()) continue;
        Result<std::u16string> windows = paths.to_windows(host.lexically_normal());
        if (windows) arg.value = utf16_to_utf8(*windows);
    }
}

LaunchArgs::LaunchArgs(LaunchArgs&& other) noexcept : args_(std::move(other.args_)) { other.args_.clear(); }

LaunchArgs& LaunchArgs::operator=(LaunchArgs&& other) noexcept {
    if (this != &other) {
        for (GameArg& arg : args_)
            if (arg.secret && arg.value) wipe(*arg.value);
        args_ = std::move(other.args_);
        other.args_.clear();
    }
    return *this;
}

LaunchArgs::~LaunchArgs() {
    for (GameArg& arg : args_)
        if (arg.secret && arg.value) wipe(*arg.value);
}

std::vector<std::string> LaunchArgs::argv() const {
    std::vector<std::string> out;
    out.reserve(args_.size());
    for (const GameArg& arg : args_) out.push_back(arg.text());
    return out;
}

Result<LaunchArgs> build_launch_args(const identity::LoginPlan& login, const SecretString& auth_password,
                                     std::string_view game_culture, std::vector<GameArg> custom_args) {
    if (auth_password.reveal().empty()) return std::unexpected(internal_bug("play.build_launch_args: empty credential"));
    for (const GameArg& arg : custom_args)
        if (reserved(arg.key))
            return std::unexpected(to_diagnostic(CustomArgsIssue{CustomArgsError::ReservedKey, 0, arg.key}));

    LaunchArgs out;
    out.args_.reserve(kFixedGameArgs.size() + 3 + custom_args.size());
    for (const FixedGameArg& fixed : kFixedGameArgs) {
        GameArg arg{std::string(fixed.key), std::nullopt, fixed.secret};
        if (fixed.value) arg.value = std::string(*fixed.value);
        if (fixed.key == kEpicLocaleKey && !game_culture.empty() && game_culture != storage::kSystemLanguage)
            arg.value = std::string(game_culture);
        out.args_.push_back(std::move(arg));
    }
    out.args_.push_back(GameArg{std::string(kAuthLoginKey), login.auth_login, false});
    out.args_.push_back(GameArg{std::string(kAuthPasswordKey), auth_password.reveal(), true});
    out.args_.push_back(GameArg{std::string(kAuthTypeKey), std::string(identity::auth_type_value(login.auth_type)), false});
    for (GameArg& arg : custom_args) push_or_replace(out.args_, std::move(arg));
    return out;
}

std::string to_log_string(const LaunchArgs& args) {
    std::string out;
    for (const GameArg& arg : args.args()) {
        if (!out.empty()) out.push_back(' ');
        out.append(arg.key);
        if (!arg.value || arg.value->empty()) continue;
        out.push_back('=');
        if (arg.secret) out.append("***");
        else if (needs_quotes(*arg.value)) append_quoted(out, *arg.value);
        else out.append(*arg.value);
    }
    return out;
}

}  // namespace rb::play
