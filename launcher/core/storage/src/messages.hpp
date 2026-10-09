#pragma once

#include "reboot/foundation/diag.hpp"

namespace rb::storage::msg {

REBOOT_MESSAGE_DECL(kWrongType);
REBOOT_MESSAGE_DECL(kUnknownName);
REBOOT_MESSAGE_DECL(kInvalidUtf8);
REBOOT_MESSAGE_DECL(kInvalidPath);
REBOOT_MESSAGE_DECL(kInvalidUuid);
REBOOT_MESSAGE_DECL(kInvalidVersion);
REBOOT_MESSAGE_DECL(kOutOfRange);
REBOOT_MESSAGE_DECL(kInvalidValue);
REBOOT_MESSAGE_DECL(kMissingMember);
REBOOT_MESSAGE_DECL(kReadOnly);
REBOOT_MESSAGE_DECL(kMemoryOnly);
REBOOT_MESSAGE_DECL(kCorrupt);
REBOOT_MESSAGE_DECL(kQuarantineFailed);
REBOOT_MESSAGE_DECL(kRestoredFromBackup);
REBOOT_MESSAGE_DECL(kSchemaBackupFailed);
REBOOT_MESSAGE_DECL(kUpgradeFailed);
REBOOT_MESSAGE_DECL(kWriteFailed);
REBOOT_MESSAGE_DECL(kCancelled);
REBOOT_MESSAGE_DECL(kRevisionConflict);
REBOOT_MESSAGE_DECL(kInvalidSetting);
REBOOT_MESSAGE_DECL(kUnknownKey);
REBOOT_MESSAGE_DECL(kInvalidConsoleKey);
REBOOT_MESSAGE_DECL(kInvalidLanguageTag);
REBOOT_MESSAGE_DECL(kInvalidLaunchArgs);
REBOOT_MESSAGE_DECL(kInvalidAuthDllPath);
REBOOT_MESSAGE_DECL(kInvalidEnvLine);
REBOOT_MESSAGE_DECL(kInvalidBackendTarget);
REBOOT_MESSAGE_DECL(kInvalidHost);
REBOOT_MESSAGE_DECL(kResetBlocked);
REBOOT_MESSAGE_DECL(kResetStopFailed);
REBOOT_MESSAGE_DECL(kRootInsidePackage);
REBOOT_MESSAGE_DECL(kRootNotWritable);
REBOOT_MESSAGE_DECL(kFrontendStateTooLarge);
REBOOT_MESSAGE_DECL(kFrontendStateNotJson);
REBOOT_MESSAGE_DECL(kInvalidShellName);

}  // namespace rb::storage::msg

namespace rb::storage {

[[nodiscard]] inline DiagBuilder invalid_input(MessageId message) {
    return make_diag(ErrorDomain::Storage, message).kind(ErrorKind::InvalidInput);
}

}  // namespace rb::storage
