#pragma once

#include "reboot/foundation/diag.hpp"

namespace reboot::gameserver::msg {

REBOOT_MESSAGE_DECL(kPathNotAbsolute);
REBOOT_MESSAGE_DECL(kExeUnreadable);
REBOOT_MESSAGE_DECL(kDescribeSpawnFailed);
REBOOT_MESSAGE_DECL(kDescribeTimeout);
REBOOT_MESSAGE_DECL(kDescribeNoOutput);
REBOOT_MESSAGE_DECL(kDescribeMalformed);
REBOOT_MESSAGE_DECL(kProtocolMismatch);
REBOOT_MESSAGE_DECL(kInvalidSockets);
REBOOT_MESSAGE_DECL(kDescriptionMismatch);
REBOOT_MESSAGE_DECL(kPortCountMismatch);
REBOOT_MESSAGE_DECL(kInvalidPort);
REBOOT_MESSAGE_DECL(kBindNotIpv4);
REBOOT_MESSAGE_DECL(kBackendRequired);
REBOOT_MESSAGE_DECL(kInvalidMatchSetting);
REBOOT_MESSAGE_DECL(kInvalidAddress);
REBOOT_MESSAGE_DECL(kSessionDirFailed);
REBOOT_MESSAGE_DECL(kAlreadyStarted);
REBOOT_MESSAGE_DECL(kNotRunning);
REBOOT_MESSAGE_DECL(kStoppedBeforeStart);
REBOOT_MESSAGE_DECL(kCommandNotDeclared);
REBOOT_MESSAGE_DECL(kCommandTimeout);

}  // namespace reboot::gameserver::msg
