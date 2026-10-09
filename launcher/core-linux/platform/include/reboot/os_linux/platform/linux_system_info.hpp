#pragma once

#include <optional>
#include <string>

#include "reboot/foundation/native_path.hpp"
#include "reboot/ports/os_services.hpp"

namespace rb::os_linux::platform {

// Covers no capability ids; ISystemInfo for the engine process, read once at construction.
class LinuxSystemInfo final : public ports::ISystemInfo {
public:
    LinuxSystemInfo();

    // NAME, VERSION_ID and BUILD_ID from /etc/os-release, else /usr/lib/os-release; the machine
    // from uname.
    [[nodiscard]] ports::OsInfo os() const override { return os_; }
    // geteuid() == 0.
    [[nodiscard]] bool elevated() const override { return elevated_; }
    // $XDG_SESSION_ID, empty under a systemd user service; play checks the caller's display instead.
    [[nodiscard]] std::string os_session() const override { return os_session_; }
    // An ancestor, walking PPid in /proc/<pid>/status up to pid 1, is Steam's `reaper` with
    // SteamLaunch on its command line. Its cleanup kills everything below it, so nothing that
    // must outlive a game may be started from there.
    [[nodiscard]] bool under_steam_reaper() const override { return under_steam_reaper_; }
    // The first readable, non-empty bundle among $SSL_CERT_FILE, /etc/ssl/certs/ca-certificates.crt
    // (Debian, Arch, SteamOS), /etc/pki/tls/certs/ca-bundle.crt (Fedora, EL),
    // /etc/ssl/ca-bundle.pem (openSUSE) and /etc/ssl/cert.pem (Alpine), since the OpenSSL we
    // ship looks in its build-time OPENSSLDIR, which no user machine has.
    [[nodiscard]] std::optional<NativePath> ca_bundle() const override { return ca_bundle_; }

    // /.dockerenv, /run/.containerenv, /.flatpak-info, or a `container` variable from the init
    // system; make_platform reads it for LinuxUpdateApplier.
    [[nodiscard]] bool in_container() const noexcept { return in_container_; }

private:
    ports::OsInfo os_;
    bool elevated_ = false;
    std::string os_session_;
    bool under_steam_reaper_ = false;
    std::optional<NativePath> ca_bundle_;
    bool in_container_ = false;
};

}  // namespace rb::os_linux::platform
