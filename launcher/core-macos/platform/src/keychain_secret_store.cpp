#include "darwin.hpp"

#include "reboot/os_macos/platform/keychain_secret_store.hpp"

#include <CoreFoundation/CoreFoundation.h>
#include <Security/Security.h>

#include <utility>
#include <vector>

#include "cf_ptr.hpp"
#include "messages.hpp"
#include "secret_files.hpp"
#include "secret_routing.hpp"

// File-based keychains have only the SecKeychain and SecAccess APIs, which Apple marks deprecated.
#pragma clang diagnostic ignored "-Wdeprecated-declarations"

namespace reboot::os_macos::platform {

namespace {

constexpr std::string_view kServicePrefix = "dev.projectreboot.launcher.";
constexpr std::string_view kItemLabel = "Reboot Launcher";

struct Keychain {
    KeychainState state = KeychainState::Missing;
    CfPtr<SecKeychainRef> ref;
};

[[nodiscard]] Keychain open_login_keychain() {
    SecKeychainRef raw = nullptr;
    if (::SecKeychainCopyDefault(&raw) != errSecSuccess || raw == nullptr) return {};
    Keychain keychain{KeychainState::Missing, CfPtr<SecKeychainRef>{raw}};
    SecKeychainStatus status = 0;
    if (::SecKeychainGetStatus(raw, &status) != errSecSuccess) return keychain;
    keychain.state = (status & kSecUnlockStateStatus) != 0 ? KeychainState::Unlocked : KeychainState::Locked;
    return keychain;
}

[[nodiscard]] Diagnostic locked() {
    return make_diag(ErrorDomain::Platform, kKeychainLocked).retryable().build();
}

[[nodiscard]] Diagnostic keychain_failed(std::string_view call, OSStatus status) {
    // Without user interaction a keychain that locked meanwhile reports this; an ACL refusal does too.
    if (status == errSecInteractionNotAllowed && open_login_keychain().state == KeychainState::Locked) return locked();
    return make_diag(ErrorDomain::Platform, kCallFailed)
        .arg("call", call)
        .os(SystemError{SystemError::Origin::Host, status})
        .build();
}

using MutableDictionary = CfPtr<CFMutableDictionaryRef>;

[[nodiscard]] MutableDictionary new_dictionary() {
    return MutableDictionary{::CFDictionaryCreateMutable(kCFAllocatorDefault, 0, &kCFTypeDictionaryKeyCallBacks,
                                                         &kCFTypeDictionaryValueCallBacks)};
}

// The generic-password item of `key` in this data root's service, searched in `keychain` only.
[[nodiscard]] Result<MutableDictionary> item_query(const std::string& service, std::string_view key, SecKeychainRef keychain) {
    MutableDictionary query = new_dictionary();
    CfPtr<CFStringRef> service_name = cf_string(service);
    CfPtr<CFStringRef> account = cf_string(key);
    const void* search[] = {keychain};
    CfPtr<CFArrayRef> search_list{::CFArrayCreate(kCFAllocatorDefault, search, 1, &kCFTypeArrayCallBacks)};
    if (!query || !service_name || !account || !search_list)
        return make_diag(ErrorDomain::Platform, kCallFailed).arg("call", "CFStringCreateWithBytes").kind(ErrorKind::InvalidInput).fail();
    ::CFDictionarySetValue(query.get(), kSecClass, kSecClassGenericPassword);
    ::CFDictionarySetValue(query.get(), kSecAttrService, service_name.get());
    ::CFDictionarySetValue(query.get(), kSecAttrAccount, account.get());
    ::CFDictionarySetValue(query.get(), kSecMatchSearchList, search_list.get());
    return query;
}

// An ACL naming this executable; for signed code the keychain stores its designated requirement.
[[nodiscard]] Result<CfPtr<SecAccessRef>> own_access() {
    SecTrustedApplicationRef self = nullptr;
    if (const OSStatus status = ::SecTrustedApplicationCreateFromPath(nullptr, &self); status != errSecSuccess)
        return std::unexpected(keychain_failed("SecTrustedApplicationCreateFromPath", status));
    CfPtr<SecTrustedApplicationRef> owned_self{self};
    const void* trusted[] = {self};
    CfPtr<CFArrayRef> trusted_list{::CFArrayCreate(kCFAllocatorDefault, trusted, 1, &kCFTypeArrayCallBacks)};
    CfPtr<CFStringRef> label = cf_string(kItemLabel);
    SecAccessRef access = nullptr;
    if (const OSStatus status = ::SecAccessCreate(label.get(), trusted_list.get(), &access); status != errSecSuccess)
        return std::unexpected(keychain_failed("SecAccessCreate", status));
    return CfPtr<SecAccessRef>{access};
}

[[nodiscard]] Result<void> keychain_put(const std::string& service, std::string_view key, std::span<const u8> value,
                                        SecKeychainRef keychain) {
    Result<MutableDictionary> query = item_query(service, key, keychain);
    if (!query) return std::unexpected(std::move(query.error()));
    CfPtr<CFDataRef> data{::CFDataCreate(kCFAllocatorDefault, value.data(), static_cast<CFIndex>(value.size()))};
    MutableDictionary update = new_dictionary();
    ::CFDictionarySetValue(update.get(), kSecValueData, data.get());
    const OSStatus updated = ::SecItemUpdate(query->get(), update.get());
    if (updated == errSecSuccess) return {};
    if (updated != errSecItemNotFound) return std::unexpected(keychain_failed("SecItemUpdate", updated));

    Result<CfPtr<SecAccessRef>> access = own_access();
    if (!access) return std::unexpected(std::move(access.error()));
    CfPtr<CFStringRef> label = cf_string(kItemLabel);
    MutableDictionary add = new_dictionary();
    ::CFDictionarySetValue(add.get(), kSecClass, kSecClassGenericPassword);
    ::CFDictionarySetValue(add.get(), kSecAttrService, ::CFDictionaryGetValue(query->get(), kSecAttrService));
    ::CFDictionarySetValue(add.get(), kSecAttrAccount, ::CFDictionaryGetValue(query->get(), kSecAttrAccount));
    ::CFDictionarySetValue(add.get(), kSecAttrLabel, label.get());
    ::CFDictionarySetValue(add.get(), kSecAttrAccess, access->get());
    ::CFDictionarySetValue(add.get(), kSecUseKeychain, keychain);
    ::CFDictionarySetValue(add.get(), kSecValueData, data.get());
    if (const OSStatus added = ::SecItemAdd(add.get(), nullptr); added != errSecSuccess)
        return std::unexpected(keychain_failed("SecItemAdd", added));
    return {};
}

[[nodiscard]] Result<std::optional<SecretBytes>> keychain_get(const std::string& service, std::string_view key,
                                                              SecKeychainRef keychain) {
    Result<MutableDictionary> query = item_query(service, key, keychain);
    if (!query) return std::unexpected(std::move(query.error()));
    ::CFDictionarySetValue(query->get(), kSecReturnData, kCFBooleanTrue);
    ::CFDictionarySetValue(query->get(), kSecMatchLimit, kSecMatchLimitOne);
    CFTypeRef result = nullptr;
    const OSStatus status = ::SecItemCopyMatching(query->get(), &result);
    if (status == errSecItemNotFound) return std::optional<SecretBytes>{};
    if (status != errSecSuccess) return std::unexpected(keychain_failed("SecItemCopyMatching", status));
    CfPtr<CFTypeRef> owned{result};
    if (result == nullptr || ::CFGetTypeID(result) != ::CFDataGetTypeID())
        return std::unexpected(keychain_failed("SecItemCopyMatching", errSecDecode));
    const auto data = static_cast<CFDataRef>(result);
    const u8* bytes = ::CFDataGetBytePtr(data);
    std::vector<u8> value(bytes, bytes + ::CFDataGetLength(data));
    return std::optional<SecretBytes>{SecretBytes{std::move(value)}};
}

[[nodiscard]] Result<void> keychain_erase(const std::string& service, std::string_view key, SecKeychainRef keychain) {
    Result<MutableDictionary> query = item_query(service, key, keychain);
    if (!query) return std::unexpected(std::move(query.error()));
    const OSStatus status = ::SecItemDelete(query->get());
    if (status == errSecSuccess || status == errSecItemNotFound) return {};
    return std::unexpected(keychain_failed("SecItemDelete", status));
}

}  // namespace

KeychainSecretStore::KeychainSecretStore(std::string root_hash16, NativePath fallback_dir, ports::IFileSystem& fs,
                                         bool aqua_session)
    : service_(std::string(kServicePrefix) + root_hash16),
      fallback_dir_(std::move(fallback_dir)),
      fs_(fs),
      aqua_session_(aqua_session) {
    ::SecKeychainSetUserInteractionAllowed(false);
}

ports::SecretStoreKind KeychainSecretStore::kind() const {
    return store_kind(open_login_keychain().state, aqua_session_);
}

Result<void> KeychainSecretStore::put(std::string_view key, std::span<const u8> value) {
    SecretFiles files(fallback_dir_, fs_);
    const Keychain keychain = open_login_keychain();
    switch (write_backend(keychain.state, aqua_session_)) {
        case SecretBackend::File:
            return files.put(key, value);
        case SecretBackend::Locked:
            return std::unexpected(locked());
        case SecretBackend::Keychain:
            break;
    }
    if (Result<void> stored = keychain_put(service_, key, value, keychain.ref.get()); !stored) return stored;
    // A file left from a locked period would shadow the keychain item, since get reads files first.
    return files.erase(key);
}

Result<std::optional<SecretBytes>> KeychainSecretStore::get(std::string_view key) {
    SecretFiles files(fallback_dir_, fs_);
    Result<std::optional<SecretBytes>> from_file = files.get(key);
    if (!from_file || from_file->has_value()) return from_file;
    const Keychain keychain = open_login_keychain();
    switch (read_backend_after_file_miss(keychain.state)) {
        case SecretBackend::File:
            return std::optional<SecretBytes>{};
        case SecretBackend::Locked:
            return std::unexpected(locked());
        case SecretBackend::Keychain:
            break;
    }
    return keychain_get(service_, key, keychain.ref.get());
}

Result<void> KeychainSecretStore::erase(std::string_view key) {
    SecretFiles files(fallback_dir_, fs_);
    if (Result<void> erased = files.erase(key); !erased) return erased;
    // A locked keychain may still hold the key, so erasing cannot report success.
    const Keychain keychain = open_login_keychain();
    switch (read_backend_after_file_miss(keychain.state)) {
        case SecretBackend::File:
            return {};
        case SecretBackend::Locked:
            return std::unexpected(locked());
        case SecretBackend::Keychain:
            break;
    }
    return keychain_erase(service_, key, keychain.ref.get());
}

}  // namespace reboot::os_macos::platform
