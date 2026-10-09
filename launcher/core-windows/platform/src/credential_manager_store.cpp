#include "win32.hpp"  // first: sets _WIN32_WINNT before any std header pulls in <windows.h>

#include "reboot/os_windows/platform/credential_manager_store.hpp"

#include <array>
#include <vector>

#include "dpapi_file.hpp"
#include "messages.hpp"
#include "reboot/ports/file_system.hpp"
#include "wide.hpp"
#include "win_error.hpp"

namespace reboot::os_windows::platform {

namespace {

// Network and SSH logons have no credential set; Credential Manager then holds nothing to persist.
[[nodiscard]] bool credential_set_available() noexcept {
    std::array<DWORD, CRED_TYPE_MAXIMUM> persist{};
    if (CredGetSessionTypes(static_cast<DWORD>(persist.size()), persist.data()) == 0) return false;
    return persist[CRED_TYPE_GENERIC] >= CRED_PERSIST_LOCAL_MACHINE;
}

[[nodiscard]] bool absent(DWORD error) noexcept { return error == ERROR_NOT_FOUND || error == ERROR_NO_SUCH_LOGON_SESSION; }

}  // namespace

CredentialManagerStore::CredentialManagerStore(std::string root_hash16, NativePath fallback_dir, ports::IFileSystem& fs)
    : target_prefix_("RebootLauncher/" + root_hash16 + "/"),
      fallback_dir_(std::move(fallback_dir)),
      fs_(fs),
      credential_manager_available_(credential_set_available()) {}

ports::SecretStoreKind CredentialManagerStore::kind() const {
    return credential_manager_available_ ? ports::SecretStoreKind::Os : ports::SecretStoreKind::File;
}

Result<void> CredentialManagerStore::put(std::string_view key, std::span<const u8> value) {
    const std::string target = target_prefix_ + std::string(key);
    const std::wstring wide_target = widen(target);
    const NativePath file = fallback_dir_ / dpapi_file_name(target);
    if (credential_manager_available_) {
        if (value.size() > CRED_MAX_CREDENTIAL_BLOB_SIZE)
            return make_diag(ErrorDomain::Platform, kSecretTooLarge)
                .arg("size", static_cast<u64>(value.size()))
                .arg("limit", static_cast<u64>(CRED_MAX_CREDENTIAL_BLOB_SIZE))
                .kind(ErrorKind::InvalidInput)
                .fail();
        CREDENTIALW credential{};
        credential.Type = CRED_TYPE_GENERIC;
        credential.TargetName = const_cast<wchar_t*>(wide_target.c_str());
        credential.CredentialBlobSize = static_cast<DWORD>(value.size());
        credential.CredentialBlob = const_cast<BYTE*>(value.data());
        credential.Persist = CRED_PERSIST_LOCAL_MACHINE;
        credential.UserName = const_cast<wchar_t*>(L"Reboot Launcher");
        if (CredWriteW(&credential, 0) == 0) return std::unexpected(call_failed("CredWriteW", GetLastError()));
        return fs_.remove_tree(file);
    }
    auto sealed = dpapi_seal(value, target);
    if (!sealed) return std::unexpected(std::move(sealed.error()));
    if (auto made = fs_.create_dirs_owner_only(fallback_dir_); !made) return made;
    if (auto written = fs_.atomic_replace(file, *sealed, false); !written) return written;
    // A copy an interactive session wrote stays unreachable from here; deleting it is best effort.
    CredDeleteW(wide_target.c_str(), CRED_TYPE_GENERIC, 0);
    return {};
}

Result<std::optional<SecretBytes>> CredentialManagerStore::get(std::string_view key) {
    const std::string target = target_prefix_ + std::string(key);
    const std::wstring wide_target = widen(target);

    const auto from_credential_manager = [&]() -> Result<std::optional<SecretBytes>> {
        CREDENTIALW* credential = nullptr;
        if (CredReadW(wide_target.c_str(), CRED_TYPE_GENERIC, 0, &credential) == 0) {
            const DWORD error = GetLastError();
            if (absent(error)) return std::optional<SecretBytes>{};
            return std::unexpected(call_failed("CredReadW", error));
        }
        std::vector<u8> bytes(credential->CredentialBlob, credential->CredentialBlob + credential->CredentialBlobSize);
        SecureZeroMemory(credential->CredentialBlob, credential->CredentialBlobSize);
        CredFree(credential);
        return std::optional<SecretBytes>{SecretBytes(std::move(bytes))};
    };
    const auto from_file = [&]() -> Result<std::optional<SecretBytes>> {
        auto sealed = fs_.read_all(fallback_dir_ / dpapi_file_name(target));
        if (!sealed) {
            if (sealed.error().kind == ErrorKind::NotFound) return std::optional<SecretBytes>{};
            return std::unexpected(std::move(sealed.error()));
        }
        auto plain = dpapi_open(*sealed, target);
        if (!plain)
            return make_diag(ErrorDomain::Platform, kSecretUnreadable).arg("key", key).cause(std::move(plain.error())).fail();
        return std::optional<SecretBytes>{std::move(*plain)};
    };

    if (credential_manager_available_) {
        auto found = from_credential_manager();
        if (!found || *found) return found;
        return from_file();
    }
    auto found = from_file();
    if (!found || *found) return found;
    // Without a credential set CredReadW cannot succeed, but a failing read must not hide the miss.
    auto stored = from_credential_manager();
    if (!stored) return std::optional<SecretBytes>{};
    return stored;
}

Result<void> CredentialManagerStore::erase(std::string_view key) {
    const std::string target = target_prefix_ + std::string(key);
    const std::wstring wide_target = widen(target);
    if (CredDeleteW(wide_target.c_str(), CRED_TYPE_GENERIC, 0) == 0) {
        const DWORD error = GetLastError();
        if (credential_manager_available_ && !absent(error)) return std::unexpected(call_failed("CredDeleteW", error));
    }
    return fs_.remove_tree(fallback_dir_ / dpapi_file_name(target));
}

}  // namespace reboot::os_windows::platform
