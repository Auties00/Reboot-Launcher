#pragma once

#include <string>
#include <string_view>
#include <vector>

#include "reboot/foundation/secret.hpp"
#include "reboot/ports/process.hpp"
#include "reboot/process/env_layer.hpp"

namespace rb::process {

class EnvBuilder;

// Covers no capability ids; the environment EnvBuilder produced, sorted by name. Move-only so the
// secret values it may carry have one owner, and they are wiped on destruction.
class BuiltEnv {
public:
    BuiltEnv() = default;
    BuiltEnv(BuiltEnv&& other) noexcept;
    BuiltEnv& operator=(BuiltEnv&& other) noexcept;
    BuiltEnv(const BuiltEnv&) = delete;
    BuiltEnv& operator=(const BuiltEnv&) = delete;
    ~BuiltEnv();

    [[nodiscard]] EnvSyntax syntax() const noexcept { return syntax_; }
    [[nodiscard]] const ports::EnvBlock& vars() const noexcept { return vars_; }
    // True for values set through EnvBuilder::channel_secret; to_log_string masks them.
    [[nodiscard]] bool sensitive(std::string_view name) const noexcept;
    // Names the deny-list removed, for the spawn's debug log.
    [[nodiscard]] const std::vector<std::string>& denied() const noexcept { return denied_; }

    // UTF-16LE "NAME=VALUE\0" entries in case-insensitive name order, then a final \0
    // (winhost SpawnGame.env_block_utf16).
    [[nodiscard]] SecretBytes windows_block() const;
    // For IRunnerPlatform::runner_launch's `base`; wrap the launch it returns in a WipingLaunch.
    [[nodiscard]] ports::EnvBlock copy() const;

private:
    friend class EnvBuilder;

    void wipe() noexcept;

    EnvSyntax syntax_ = EnvSyntax::Posix;
    ports::EnvBlock vars_;
    std::vector<std::string> sensitive_;
    std::vector<std::string> denied_;
};

}  // namespace rb::process
