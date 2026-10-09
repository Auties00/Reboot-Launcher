#include "darwin.hpp"

#include "reboot/os_macos/platform/mac_system_info.hpp"

#include <CoreFoundation/CoreFoundation.h>
#include <Security/AuthSession.h>
#include <Security/Security.h>

#include <array>
#include <span>
#include <string>
#include <vector>

#include "pem.hpp"
#include "reboot/foundation/log.hpp"
#include "reboot/ports/file_system.hpp"
#include "sysctl_value.hpp"
#include "trust_rules.hpp"

// CFSTR, callerSecuritySession and the trust-settings keys are C-style casts in Apple's macros.
#pragma clang diagnostic ignored "-Wold-style-cast"

namespace rb::os_macos::platform {

namespace {

[[nodiscard]] std::vector<u8> der_of(SecCertificateRef certificate) {
    CFDataRef data = ::SecCertificateCopyData(certificate);
    if (data == nullptr) return {};
    const u8* bytes = ::CFDataGetBytePtr(data);
    std::vector<u8> der(bytes, bytes + ::CFDataGetLength(data));
    ::CFRelease(data);
    return der;
}

[[nodiscard]] bool is_ssl_policy(CFTypeRef value) {
    if (value == nullptr || ::CFGetTypeID(value) != ::SecPolicyGetTypeID()) return false;
    CFDictionaryRef properties = ::SecPolicyCopyProperties(static_cast<SecPolicyRef>(const_cast<void*>(value)));
    if (properties == nullptr) return false;
    const CFTypeRef oid = ::CFDictionaryGetValue(properties, kSecPolicyOid);
    const bool ssl = oid != nullptr && ::CFEqual(oid, kSecPolicyAppleSSL);
    ::CFRelease(properties);
    return ssl;
}

[[nodiscard]] TrustSettingsEntry entry_of(CFDictionaryRef settings) {
    TrustSettingsEntry entry;
    const CFTypeRef policy = ::CFDictionaryGetValue(settings, kSecTrustSettingsPolicy);
    entry.applies_to_ssl = policy == nullptr || is_ssl_policy(policy);
    entry.constrained = ::CFDictionaryContainsKey(settings, kSecTrustSettingsPolicyString) ||
                        ::CFDictionaryContainsKey(settings, kSecTrustSettingsApplication);
    const CFTypeRef result = ::CFDictionaryGetValue(settings, kSecTrustSettingsResult);
    if (result != nullptr && ::CFGetTypeID(result) == ::CFNumberGetTypeID()) {
        SInt32 value = 0;
        if (::CFNumberGetValue(static_cast<CFNumberRef>(result), kCFNumberSInt32Type, &value)) entry.result = value;
    }
    return entry;
}

[[nodiscard]] TrustVerdict verdict_of(SecCertificateRef certificate, SecTrustSettingsDomain domain) {
    CFArrayRef settings = nullptr;
    if (::SecTrustSettingsCopyTrustSettings(certificate, domain, &settings) != errSecSuccess || settings == nullptr)
        return TrustVerdict::Unspecified;
    std::vector<TrustSettingsEntry> entries;
    const CFIndex count = ::CFArrayGetCount(settings);
    for (CFIndex i = 0; i < count; ++i) {
        const void* value = ::CFArrayGetValueAtIndex(settings, i);
        if (value != nullptr && ::CFGetTypeID(value) == ::CFDictionaryGetTypeID())
            entries.push_back(entry_of(static_cast<CFDictionaryRef>(value)));
    }
    ::CFRelease(settings);
    return classify_trust_settings(entries);
}

// System roots, then the admin and user trust settings that add to or deny them.
[[nodiscard]] std::vector<std::vector<u8>> ssl_anchors() {
    constexpr std::array<SecTrustSettingsDomain, 3> kDomains{kSecTrustSettingsDomainSystem, kSecTrustSettingsDomainAdmin,
                                                             kSecTrustSettingsDomainUser};
    std::vector<CertificateVerdict> verdicts;
    for (const SecTrustSettingsDomain domain : kDomains) {
        CFArrayRef certificates = nullptr;
        if (::SecTrustSettingsCopyCertificates(domain, &certificates) != errSecSuccess || certificates == nullptr) continue;
        const CFIndex count = ::CFArrayGetCount(certificates);
        for (CFIndex i = 0; i < count; ++i) {
            auto* certificate = static_cast<SecCertificateRef>(const_cast<void*>(::CFArrayGetValueAtIndex(certificates, i)));
            std::vector<u8> der = der_of(certificate);
            if (der.empty()) continue;
            const TrustVerdict verdict =
                domain == kSecTrustSettingsDomainSystem ? TrustVerdict::Trusted : verdict_of(certificate, domain);
            verdicts.push_back({std::move(der), verdict});
        }
        ::CFRelease(certificates);
    }
    return trusted_anchors(verdicts);
}

[[nodiscard]] std::optional<NativePath> export_anchors(const NativePath& trust_dir, ports::IFileSystem& fs) {
    const std::vector<std::vector<u8>> anchors = ssl_anchors();
    if (anchors.empty()) {
        REBOOT_LOG_WARN(Engine, "no SSL trust anchors could be read from the keychains");
        return std::nullopt;
    }
    const std::string pem = pem_bundle(anchors);
    const NativePath path = trust_dir / "apple-roots.pem";
    const std::span<const u8> bytes(reinterpret_cast<const u8*>(pem.data()), pem.size());
    if (Result<void> created = fs.create_dirs_owner_only(trust_dir); !created) {
        REBOOT_LOG_WARN(Engine, "the trust directory could not be created: {}", created.error().id);
        return std::nullopt;
    }
    if (Result<void> written = fs.atomic_replace(path, bytes, false); !written) {
        REBOOT_LOG_WARN(Engine, "the SSL trust anchors could not be exported: {}", written.error().id);
        return std::nullopt;
    }
    return path;
}

}  // namespace

MacSystemInfo::MacSystemInfo(const NativePath& trust_dir, ports::IFileSystem& fs) {
    os_.name = "macOS";
    os_.version = sysctl_string("kern.osproductversion").value_or("");
    os_.build = sysctl_string("kern.osversion").value_or("");
    os_.arch = apple_silicon() ? "arm64" : "x86_64";
    elevated_ = ::geteuid() == 0;
    SecuritySessionId session = 0;
    SessionAttributeBits attributes = 0;
    if (::SessionGetInfo(callerSecuritySession, &session, &attributes) == errSessionSuccess) {
        os_session_ = std::to_string(session);
        aqua_ = (attributes & sessionHasGraphicAccess) != 0;
    }
    ca_bundle_ = export_anchors(trust_dir, fs);
}

ports::OsInfo MacSystemInfo::os() const { return os_; }

bool MacSystemInfo::elevated() const { return elevated_; }

std::string MacSystemInfo::os_session() const { return os_session_; }

}  // namespace rb::os_macos::platform
