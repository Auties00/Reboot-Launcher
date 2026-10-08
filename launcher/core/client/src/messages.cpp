#include "messages.hpp"

namespace reboot::client::msg {

REBOOT_MESSAGE(kInvalidArgument, "client.invalid_argument", "The argument {name} is invalid.");
REBOOT_MESSAGE(kAbiMismatch, "client.abi_mismatch", "The caller was built for a newer reboot_client than ABI {abi_version}.");
REBOOT_MESSAGE(kCallTimedOut, "client.call_timed_out", "The engine did not answer method {method} within {timeout}.");
REBOOT_MESSAGE(kClosed, "client.closed", "The engine connection is closed.");

}  // namespace reboot::client::msg
