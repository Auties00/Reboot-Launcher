#include "messages.hpp"

namespace rb::trust {

REBOOT_MESSAGE(kSignatureMalformed, "trust.signature_malformed", "The signature file of the {document} is malformed");
REBOOT_MESSAGE(kUnknownKey, "trust.unknown_key",
               "The {document} is signed with key {key_id}, which this launcher version does not trust");
REBOOT_MESSAGE(kSignatureInvalid, "trust.signature_invalid",
               "The {document} does not match its signature by key {key_id}");
REBOOT_MESSAGE(kSerialRollback, "trust.serial_rollback",
               "The {document} has serial {serial}, older than serial {highest_seen} already accepted");
REBOOT_MESSAGE(kSerialPersistFailed, "trust.serial_persist_failed",
               "Could not record serial {serial} of the {document}");
REBOOT_MESSAGE(kCryptoFailure, "trust.crypto_failure", "OpenSSL failed while checking the {document} signature");
REBOOT_MESSAGE(kDocumentExpired, "trust.document_expired",
               "The {document} expired {expired_for} ago; the last verified copy stays in use");

}  // namespace rb::trust
