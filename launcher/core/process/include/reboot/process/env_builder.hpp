#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "reboot/contracts/ipc.hpp"
#include "reboot/foundation/diag.hpp"
#include "reboot/foundation/native_path.hpp"
#include "reboot/foundation/secret.hpp"
#include "reboot/ports/process.hpp"
#include "reboot/process/built_env.hpp"
#include "reboot/process/env_layer.hpp"
#include "reboot/storage/settings_values.hpp"

namespace reboot::process {

inline constexpr std::string_view kOpensslIa32capName = "OPENSSL_ia32cap";
inline constexpr std::string_view kOpensslIa32capValue = "~0x20000000";
inline constexpr std::string_view kWineDebugName = "WINEDEBUG";
inline constexpr std::string_view kWineDebugDefault = "fixme-all";
inline constexpr std::string_view kProtonLogName = "PROTON_LOG";
inline constexpr std::string_view kProtonLogDirName = "PROTON_LOG_DIR";

// Capabilities: game-launch.process-spawn-helper.
// The one source of every child environment: layers go in EnvLayer order, each filtered by its
// list in env_layer.hpp, and kDenyList is applied last over all of them. Names the builder owns
// are dropped from every input and set only here:
// - OPENSSL_ia32cap: kOpensslIa32capValue when the build opts in, never inherited;
// - WINEDEBUG: runner launches only, the profile's value or else kWineDebugDefault, never inherited;
// - PROTON_LOG and PROTON_LOG_DIR: only through proton_log().
// Setting a name twice in one layer keeps the last value. Strand-only. Wipes the secret values it
// holds on destruction.
class EnvBuilder {
public:
    explicit EnvBuilder(EnvSyntax syntax);
    EnvBuilder(EnvBuilder&& other) noexcept;
    EnvBuilder& operator=(EnvBuilder&& other) noexcept;
    EnvBuilder(const EnvBuilder&) = delete;
    EnvBuilder& operator=(const EnvBuilder&) = delete;
    ~EnvBuilder();

    // An independent copy of every layer so far, so one base yields both the winhost and the game
    // environment, each with its own channel layer.
    [[nodiscard]] EnvBuilder fork() const;

    // Layer 1: the user's variables from the platform. Invalid base names (Windows "=C:") are skipped.
    EnvBuilder& daemon_base(const ports::EnvBlock& user_environment);
    // Layer 2: the display and session variables the client captured in Hello.
    EnvBuilder& client(const contracts::ipc::CallerContext& caller);
    // Layer 3: the per-profile user pass-through.
    EnvBuilder& profile(const ports::EnvBlock& pass_through);
    // Layer 4: RuntimeLayout::env, the runner's own settings and whatever IRunnerPlatform::runner_launch
    // put in its launch, so the deny-list still runs last. Marks this as a Wine launch.
    EnvBuilder& runner(const ports::EnvBlock& vars);
    // Layer 5: a REBOOT_ name; on a Wine launch only the kWineChannelNames.
    EnvBuilder& channel(std::string_view name, std::string value);
    // Layer 5, masked in logs and wiped with the BuiltEnv (REBOOT_CTL_TOKEN).
    EnvBuilder& channel_secret(std::string_view name, SecretString value);

    // The catalog's BuildFlags.openssl_ia32cap; off unless set.
    EnvBuilder& openssl_ia32cap(bool opted_in);
    // The "Verbose Wine logging" opt-in: PROTON_LOG=1 and PROTON_LOG_DIR=`log_dir`.
    EnvBuilder& proton_log(const NativePath& log_dir);
    // The session's pinned play settings: play.env as layer 3 and play.verbose_wine_log as
    // proton_log(`wine_log_dir`), which only a runner launch uses.
    EnvBuilder& play_settings(const storage::PlaySettings& play, const NativePath& wine_log_dir);

    // Fails on an invalid name or value outside the base (empty, '=' or NUL in a name, NUL or
    // invalid UTF-8 anywhere) and on a channel name outside the rule of channel().
    [[nodiscard]] Result<BuiltEnv> build() &&;

private:
    struct Entry {
        EnvLayer layer{};
        std::string name;
        std::string value;
        bool sensitive = false;
    };

    void set(EnvLayer layer, std::string_view name, std::string value, bool sensitive);
    void wipe() noexcept;

    EnvSyntax syntax_;
    std::vector<Entry> entries_;
    bool runner_launch_ = false;
    bool openssl_ia32cap_ = false;
    std::optional<NativePath> proton_log_dir_;
};

}  // namespace reboot::process
