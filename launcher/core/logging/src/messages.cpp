#include "messages.hpp"

namespace rb::logging::msg {

REBOOT_MESSAGE(kDirectoryFailed, "logging.directory_failed", "Cannot create the log folder {path}");
REBOOT_MESSAGE(kOpenFailed, "logging.open_failed", "Cannot open the log file {path}");
REBOOT_MESSAGE(kWriteFailed, "logging.write_failed", "Cannot write the log file {path}");
REBOOT_MESSAGE(kExportDestinationInvalid, "logging.export_destination_invalid",
               "The log export destination {path} must be an absolute .zip path outside the log folder");
REBOOT_MESSAGE(kExportWriteFailed, "logging.export_write_failed", "Cannot write the log export {path}");
REBOOT_MESSAGE(kExportReadFailed, "logging.export_read_failed", "Cannot read the log file {path} for the export");
REBOOT_MESSAGE(kExportCancelled, "logging.export_cancelled", "The log export to {path} was cancelled");

}  // namespace rb::logging::msg
