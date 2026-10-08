#include "messages.hpp"

namespace reboot::msg {

REBOOT_MESSAGE(kInternalBug, "internal.bug", "Something went wrong inside the launcher ({where}).");
REBOOT_MESSAGE(kInvalidUuid, "foundation.invalid_uuid", "{text} is not a valid identifier.");
REBOOT_MESSAGE(kInvalidSemVer, "foundation.invalid_semver", "{text} is not a valid version number.");
REBOOT_MESSAGE(kInvalidGameVersion, "foundation.invalid_game_version", "{text} is not a valid game version.");
REBOOT_MESSAGE(kMalformedWirePath, "foundation.malformed_wire_path", "A path received from another process is malformed.");
REBOOT_MESSAGE(kDataRootNotAbsolute, "foundation.data_root_not_absolute",
               "The data folder {path} set in REBOOT_LAUNCHER_HOME is not an absolute path.");
REBOOT_MESSAGE(kNoDataRoot, "foundation.no_data_root", "This system does not report a folder for the launcher's data.");
REBOOT_MESSAGE(kOpNotFound, "foundation.op_not_found", "Operation {op} does not exist.");
REBOOT_MESSAGE(kRequestNotFound, "requests.not_found", "Request {request} does not exist.");
REBOOT_MESSAGE(kRequestAlreadyResolved, "requests.already_resolved",
               "Request {request} was already answered or withdrawn.");

// framing.hpp names these ids for every contract package; they are registered here.
REBOOT_MESSAGE(kMalformedFrame, "contracts.malformed_frame", "A message of type {frame_type} could not be decoded.");
REBOOT_MESSAGE(kUnexpectedFrame, "contracts.unexpected_frame",
               "A message of type {actual} arrived where type {expected} was expected.");

}  // namespace reboot::msg
