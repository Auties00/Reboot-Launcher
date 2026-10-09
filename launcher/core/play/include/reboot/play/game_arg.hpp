#pragma once

#include <array>
#include <optional>
#include <string>
#include <string_view>

namespace reboot::play {

// Covers game-launch.arguments.
// One game argument, written "key" or "key=value". Keys compare ASCII case-insensitively, as
// UE's FParse does.
struct GameArg {
    std::string key;
    // Absent or empty writes the bare key.
    std::optional<std::string> value;
    // Masked by to_log_string and wiped with its LaunchArgs: the credential and -caldera.
    bool secret = false;

    // Unquoted; the session host quotes the value for the Windows command line.
    [[nodiscard]] std::string text() const;

    bool operator==(const GameArg&) const = default;
};

struct FixedGameArg {
    std::string_view key;
    std::optional<std::string_view> value;
    // Copied to GameArg::secret.
    bool secret = false;
};

// Fixed literals the game checks on every launch, byte for byte.
inline constexpr std::string_view kFlToken = "3db3ba5dcbd2e16703f3978d";
inline constexpr std::string_view kCalderaToken =
    "eyJhbGciOiJFUzI1NiIsInR5cCI6IkpXVCJ9."
    "eyJhY2NvdW50X2lkIjoiYmU5ZGE1YzJmYmVhNDQwN2IyZjQwZWJhYWQ4NTlhZDQiLCJnZW5lcmF0ZWQiOjE2Mzg3MTcyNzgsImNhbGRlcmFHdWlk"
    "IjoiMzgxMGI4NjMtMmE2NS00NDU3LTliNTgtNGRhYjNiNDgyYTg2IiwiYWNQcm92aWRlciI6IkVhc3lBbnRpQ2hlYXQiLCJub3RlcyI6IiIsImZh"
    "bGxiYWNrIjpmYWxzZX0."
    "VAWQB67RTxhiWOxx7DBjnzDnXyyEnX7OljJm-j2d88G_WgwQ9wrE6lwMEHZHjBd1ISJdUO1UVUqkfLdU5nofBQ";

// Covers game-launch.arguments, game-launch.+10.
// The tokens every play launch starts with, in this order. -caldera is masked in logs as
// game-launch.arguments requires, though the literal is public.
inline constexpr std::array<FixedGameArg, 9> kFixedGameArgs{{
    {"-epicapp", "Fortnite"},
    {"-epicenv", "Prod"},
    // build_launch_args replaces the value with ClientSettings::game_culture unless that is "system".
    {"-epiclocale", "en-us"},
    {"-epicportal", std::nullopt},
    {"-skippatchcheck", std::nullopt},
    {"-nobe", std::nullopt},
    {"-fromfl", "eac"},
    {"-fltoken", kFlToken},
    {"-caldera", kCalderaToken, true},
}};

// Set only by the engine; a custom argument with one of these keys is refused.
inline constexpr std::string_view kAuthLoginKey = "-AUTH_LOGIN";
inline constexpr std::string_view kAuthPasswordKey = "-AUTH_PASSWORD";
inline constexpr std::string_view kAuthTypeKey = "-AUTH_TYPE";

}  // namespace reboot::play
