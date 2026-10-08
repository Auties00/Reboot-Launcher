#pragma once

namespace reboot::compat {

// The UserRequest payload of UserRequestKind::RosettaInstall: the UI asks the user to install
// Rosetta 2 and answers with a RosettaInstallAnswer.
struct RosettaInstallRequest {
    bool operator==(const RosettaInstallRequest&) const = default;
};

}  // namespace reboot::compat
