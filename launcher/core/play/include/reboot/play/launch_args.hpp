#pragma once

#include <cstddef>
#include <expected>
#include <string>
#include <string_view>
#include <vector>

#include "reboot/compat/path_mapper.hpp"
#include "reboot/foundation/diag.hpp"
#include "reboot/foundation/secret.hpp"
#include "reboot/foundation/types.hpp"
#include "reboot/identity/login_plan.hpp"
#include "reboot/play/game_arg.hpp"

namespace reboot::play {

enum class CustomArgsError : u8 { UnbalancedQuote, InvalidText, ReservedKey };

struct CustomArgsIssue {
    CustomArgsError error{};
    // Byte offset of the token in the input.
    std::size_t offset = 0;
    // ReservedKey: the key as typed.
    std::string key;
};

// play.custom_args_unbalanced_quote, play.custom_args_invalid or play.custom_args_reserved.
[[nodiscard]] Diagnostic to_diagnostic(const CustomArgsIssue& issue);

// Covers game-launch.arguments.
// Shell-style, but a backslash is literal except in \", so Windows paths survive. A token splits at
// its first '='; NUL or invalid UTF-8 is InvalidText, an -AUTH_* key is ReservedKey.
[[nodiscard]] std::expected<std::vector<GameArg>, CustomArgsIssue> parse_custom_args(std::string_view text);

// Covers game-launch.arguments.
// Wine runners only: a value that is an absolute host path becomes its prefix path; an unmapped one stays.
void map_host_paths(std::vector<GameArg>& custom_args, const compat::PathMapper& paths);

class LaunchArgs {
public:
    LaunchArgs() = default;
    LaunchArgs(LaunchArgs&& other) noexcept;
    LaunchArgs& operator=(LaunchArgs&& other) noexcept;
    LaunchArgs(const LaunchArgs&) = delete;
    LaunchArgs& operator=(const LaunchArgs&) = delete;
    // Wipes the secret values.
    ~LaunchArgs();

    [[nodiscard]] const std::vector<GameArg>& args() const noexcept { return args_; }
    // GameArg::text() of each, for ports::SessionLaunch::args.
    [[nodiscard]] std::vector<std::string> argv() const;

private:
    friend Result<LaunchArgs> build_launch_args(const identity::LoginPlan&, const SecretString&, std::string_view,
                                                std::vector<GameArg>);

    std::vector<GameArg> args_;
};

// Covers game-launch.arguments, game-launch.+10.
// kFixedGameArgs, -AUTH_LOGIN, -AUTH_PASSWORD, -AUTH_TYPE, then `custom_args`; a repeated key
// replaces the earlier token's value in place, compared case-insensitively. An empty
// `auth_password` is an internal.bug, and a reserved key in `custom_args` play.custom_args_reserved.
[[nodiscard]] Result<LaunchArgs> build_launch_args(const identity::LoginPlan& login, const SecretString& auth_password,
                                                   std::string_view game_culture, std::vector<GameArg> custom_args);

// Covers game-launch.arguments.
// One line for the session log, every secret value written "***"; the Redactor still runs over it.
[[nodiscard]] std::string to_log_string(const LaunchArgs& args);

}  // namespace reboot::play
