#include "messages.hpp"

namespace reboot::identity::msg {

REBOOT_MESSAGE(kDisplayNameTooShort, "identity.display_name_too_short",
               "A player name needs at least {min} characters");
REBOOT_MESSAGE(kDisplayNameTooLong, "identity.display_name_too_long", "A player name can have at most {max} characters");
REBOOT_MESSAGE(kDisplayNameInvalidCharacter, "identity.display_name_invalid_character",
               "A player name can only contain the letters A to Z and digits");
REBOOT_MESSAGE(kBackendLoginsNotList, "identity.backend_logins_not_list", "The saved backend logins are not a list");
REBOOT_MESSAGE(kBackendLoginWithoutEndpoint, "identity.backend_login_without_endpoint",
               "A saved backend login names no backend");
REBOOT_MESSAGE(kDuplicateBackendLogin, "identity.duplicate_backend_login",
               "The {backend} backend has more than one saved login; the first one is used");
REBOOT_MESSAGE(kEmptyRemoteLogin, "identity.empty_remote_login", "The login for the {backend} backend is empty");
REBOOT_MESSAGE(kLegacyArgvNeedsCustomAuthDll, "identity.legacy_argv_needs_custom_auth_dll",
               "Passing the password on the command line needs a custom authentication DLL");
REBOOT_MESSAGE(kLegacyArgvNeedsHostedBackend, "identity.legacy_argv_needs_hosted_backend",
               "Passing the password on the command line needs a local or remote backend");
REBOOT_MESSAGE(kLegacyArgvNeedsLogin, "identity.legacy_argv_needs_login",
               "Passing the password on the command line needs the account's login");
REBOOT_MESSAGE(kPasswordInArgv, "identity.password_in_argv",
               "The password is passed on the game's command line, where other programs on this computer can read it");

}  // namespace reboot::identity::msg
