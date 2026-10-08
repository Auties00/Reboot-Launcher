#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "reboot/foundation/diag.hpp"
#include "reboot/foundation/native_path.hpp"
#include "reboot/foundation/types.hpp"

namespace reboot::ports {

class IShellLauncher {
public:
    virtual ~IShellLauncher() = default;

    // Refuses anything but https.
    virtual Result<void> open_url(std::string_view https_url) = 0;
    virtual Result<void> open_path(const NativePath& path) = 0;
    virtual Result<void> reveal(const NativePath& path) = 0;
    virtual Result<void> trash(const NativePath& path) = 0;
};

enum class IntegrationKind : u8 { UrlScheme, Autostart, EngineAgent, DesktopEntry };
enum class IntegrationState : u8 { Absent, Ours, Foreign, Stale };

struct IntegrationStatus {
    IntegrationKind kind{};
    IntegrationState state{};
    std::string detail;
};

class IIntegrationRegistrar {
public:
    virtual ~IIntegrationRegistrar() = default;

    virtual Result<IntegrationStatus> status(IntegrationKind kind) = 0;
    virtual Result<void> apply(IntegrationKind kind, const NativePath& exe) = 0;
    virtual Result<void> remove(IntegrationKind kind) = 0;
};

enum class SmartAppControl : u8 { Off, Evaluation, On, Unknown };

struct SecurityProducts {
    std::vector<std::string> names;
    SmartAppControl smart_app_control = SmartAppControl::Unknown;
};

// nullopt where the OS has no queryable security center.
class ISecurityProductProbe {
public:
    virtual ~ISecurityProductProbe() = default;
    virtual Result<std::optional<SecurityProducts>> probe() = 0;
};

struct PrerequisiteStatus {
    std::string id;
    bool met = false;
    std::optional<MessageId> remediation_message_id;
};

class IPrerequisiteProbe {
public:
    virtual ~IPrerequisiteProbe() = default;

    virtual std::vector<PrerequisiteStatus> check() = 0;
    virtual Result<void> remediate(std::string_view id) = 0;
};

struct OsInfo {
    std::string name;
    std::string version;
    std::string build;
    std::string arch;
};

class ISystemInfo {
public:
    virtual ~ISystemInfo() = default;

    [[nodiscard]] virtual OsInfo os() const = 0;
    [[nodiscard]] virtual bool elevated() const = 0;
    [[nodiscard]] virtual std::string os_session() const = 0;
    [[nodiscard]] virtual bool under_steam_reaper() const = 0;
    [[nodiscard]] virtual std::optional<NativePath> ca_bundle() const = 0;
};

class IUpdateApplier {
public:
    virtual ~IUpdateApplier() = default;

    virtual Result<void> stage(const NativePath& package) = 0;
    // Does not return on success: the process is replaced or exits for the updater.
    virtual Result<void> apply_and_restart(std::vector<std::string> args) = 0;
    [[nodiscard]] virtual bool supports_in_place() const = 0;
};

}  // namespace reboot::ports
