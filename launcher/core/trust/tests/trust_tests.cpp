#include <openssl/evp.h>

#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <fstream>
#include <iterator>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "hex.hpp"
#include "reboot/foundation/diag.hpp"
#include "reboot/foundation/sha256.hpp"
#include "reboot/trust/check_expiry.hpp"
#include "reboot/trust/key_ring.hpp"
#include "reboot/trust/serial_guard.hpp"
#include "reboot/trust/signed_document.hpp"
#include "reboot/trust/trust_error.hpp"
#include "reboot/trust/verify_signed.hpp"

using namespace rb;
using namespace rb::trust;

namespace {

struct PkeyDeleter {
    void operator()(EVP_PKEY* key) const noexcept { EVP_PKEY_free(key); }
};
struct MdCtxDeleter {
    void operator()(EVP_MD_CTX* context) const noexcept { EVP_MD_CTX_free(context); }
};

// Signs the way the CI signer does: context prefix, then the body.
class TestSigner {
public:
    TestSigner() : key_(EVP_PKEY_Q_keygen(nullptr, nullptr, "ED25519")) {
        REQUIRE(key_);
        std::size_t size = public_key_.size();
        REQUIRE(EVP_PKEY_get_raw_public_key(key_.get(), public_key_.data(), &size) == 1);
    }

    [[nodiscard]] const Ed25519PublicKey& public_key() const { return public_key_; }

    [[nodiscard]] std::string signature_file(SignedDocumentKind kind, const std::vector<u8>& body) const {
        const std::string_view prefix = signature_context(kind);
        std::vector<u8> message(prefix.begin(), prefix.end());
        message.insert(message.end(), body.begin(), body.end());

        const std::unique_ptr<EVP_MD_CTX, MdCtxDeleter> context(EVP_MD_CTX_new());
        REQUIRE(EVP_DigestSignInit(context.get(), nullptr, nullptr, nullptr, key_.get()) == 1);
        Ed25519Signature signature{};
        std::size_t size = signature.size();
        REQUIRE(EVP_DigestSign(context.get(), signature.data(), &size, message.data(), message.size()) == 1);
        return "ed25519 " + key_id_of(public_key_) + " " + to_hex(signature) + "\n";
    }

private:
    std::unique_ptr<EVP_PKEY, PkeyDeleter> key_;
    Ed25519PublicKey public_key_{};
};

std::vector<u8> bytes(std::string_view text) { return {text.begin(), text.end()}; }

std::string read_data(std::string_view name) {
    std::ifstream stream(std::string(REBOOT_TRUST_TEST_DATA) + "/" + std::string(name), std::ios::binary);
    REQUIRE(stream);
    return {std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>()};
}

SignedDocument signed_by(const TestSigner& signer, SignedDocumentKind kind, std::string_view body) {
    auto document = make_signed_document(kind, bytes(body), signer.signature_file(kind, bytes(body)));
    REQUIRE(document);
    return std::move(*document);
}

}  // namespace

TEST_CASE("verify_signed accepts the current and the next key") {
    const TestSigner current;
    const TestSigner next;
    const KeyRing ring(SignedDocumentKind::BuildCatalog, {current.public_key(), next.public_key()});

    CHECK(verify_signed(ring, signed_by(current, SignedDocumentKind::BuildCatalog, R"({"serial":1})")));
    CHECK(verify_signed(ring, signed_by(next, SignedDocumentKind::BuildCatalog, R"({"serial":2})")));
}

TEST_CASE("verify_signed accepts files signed by the openssl CLI") {
    const auto key = decode_hex<kEd25519PublicKeySize>(read_data("known_answer.pub"));
    REQUIRE(key);

    for (const auto& [kind, file] : {std::pair{SignedDocumentKind::BuildCatalog, "build_catalog.json"},
                                     std::pair{SignedDocumentKind::ReleaseManifest, "release_manifest.json"}}) {
        const KeyRing ring(kind, {*key, {}});
        const std::string body = read_data(file);
        const auto document = make_signed_document(kind, bytes(body), read_data(std::string(file) + ".sig"));
        REQUIRE(document);
        CHECK(document->key_id == key_id_of(*key));
        CHECK(verify_signed(ring, *document));
    }
}

TEST_CASE("verify_signed rejects a changed body") {
    const TestSigner signer;
    const KeyRing ring(SignedDocumentKind::BuildCatalog, {signer.public_key(), {}});
    auto document = signed_by(signer, SignedDocumentKind::BuildCatalog, R"({"serial":1})");
    document.body.back() = ']';

    const auto result = verify_signed(ring, document);
    REQUIRE_FALSE(result);
    CHECK(result.error().code == TrustErrorCode::SignatureInvalid);
    CHECK(to_diagnostic(result.error()).id == "trust.signature_invalid");
}

TEST_CASE("verify_signed rejects a key outside the ring") {
    const TestSigner pinned;
    const TestSigner stranger;
    const KeyRing ring(SignedDocumentKind::ReleaseManifest, {pinned.public_key(), {}});

    const auto result = verify_signed(ring, signed_by(stranger, SignedDocumentKind::ReleaseManifest, "{}"));
    REQUIRE_FALSE(result);
    CHECK(result.error().code == TrustErrorCode::UnknownKey);
    CHECK(result.error().key_id == key_id_of(stranger.public_key()));
}

TEST_CASE("a catalog signature never verifies as a manifest under the same key") {
    const TestSigner signer;
    const KeyRing manifest_ring(SignedDocumentKind::ReleaseManifest, {signer.public_key(), {}});

    const auto result = verify_signed(manifest_ring, signed_by(signer, SignedDocumentKind::BuildCatalog, "{}"));
    REQUIRE_FALSE(result);
    CHECK(result.error().code == TrustErrorCode::SignatureInvalid);
}

TEST_CASE("an empty ring rejects everything") {
    const TestSigner signer;
    const KeyRing ring(SignedDocumentKind::BuildCatalog, {});
    const auto result = verify_signed(ring, signed_by(signer, SignedDocumentKind::BuildCatalog, "{}"));
    REQUIRE_FALSE(result);
    CHECK(result.error().code == TrustErrorCode::UnknownKey);
}

TEST_CASE("make_signed_document parses the detached signature file") {
    const std::string id = "0123456789abcdef";
    const std::string signature(128, 'a');

    SECTION("with LF, CRLF or no line ending") {
        for (const std::string_view ending : {"\n", "\r\n", ""}) {
            const auto document = make_signed_document(SignedDocumentKind::BuildCatalog, bytes("x"),
                                                       "ed25519 " + id + " " + signature + std::string(ending));
            REQUIRE(document);
            CHECK(document->key_id == id);
            CHECK(document->signature[0] == 0xaa);
        }
    }

    SECTION("rejecting malformed files") {
        for (const std::string& text : {std::string{}, "ed448 " + id + " " + signature, "ed25519 " + signature,
                                       "ed25519 0123 " + signature, "ed25519 " + id + " " + signature.substr(2),
                                       "ed25519 " + id + " " + signature + "\n\n", "ed25519 " + id + "  " + signature,
                                       "ed25519 zz23456789abcdef " + signature,
                                       "ed25519 0123456789ABCDEF " + signature,
                                       "ed25519 " + id + " " + signature + "\r",
                                       "ed25519 " + id + " " + signature + " ",
                                       "ed25519 " + id + " " + signature + "00"}) {
            const auto document = make_signed_document(SignedDocumentKind::BuildCatalog, bytes("x"), text);
            REQUIRE_FALSE(document);
            CHECK(document.error().code == TrustErrorCode::SignatureMalformed);
        }
    }
}

TEST_CASE("SerialGuard refuses rollback and persists before advancing") {
    std::vector<u64> persisted;
    SerialGuard guard(SignedDocumentKind::ReleaseManifest, 10, [&](u64 serial) -> Result<void> {
        persisted.push_back(serial);
        return {};
    });

    CHECK(guard.admit(10) == SerialCheck::Same);
    CHECK(persisted.empty());

    CHECK(guard.admit(12) == SerialCheck::Advanced);
    CHECK(persisted == std::vector<u64>{12});
    CHECK(guard.highest_seen() == 12);

    const auto rollback = guard.admit(11);
    REQUIRE_FALSE(rollback);
    CHECK(rollback.error().code == TrustErrorCode::SerialRollback);
    CHECK(rollback.error().highest_seen == 12);
    CHECK(to_diagnostic(rollback.error()).id == "trust.serial_rollback");
}

TEST_CASE("SerialGuard does not advance when persisting fails") {
    SerialGuard guard(SignedDocumentKind::BuildCatalog, 3, [](u64) -> Result<void> {
        return std::unexpected(Diagnostic{.domain = ErrorDomain::Storage, .id = "storage.write_failed"});
    });

    const auto result = guard.admit(4);
    REQUIRE_FALSE(result);
    CHECK(result.error().code == TrustErrorCode::SerialPersistFailed);
    CHECK(guard.highest_seen() == 3);
    const Diagnostic diag = to_diagnostic(result.error());
    REQUIRE(diag.causes.size() == 1);
    CHECK(diag.causes[0].id == "storage.write_failed");
}

TEST_CASE("check_expiry warns only after the expiry") {
    const auto expires_at = std::chrono::system_clock::time_point{} + std::chrono::hours(1000);

    CHECK_FALSE(check_expiry(SignedDocumentKind::BuildCatalog, expires_at, expires_at));

    const auto warning = check_expiry(SignedDocumentKind::BuildCatalog, expires_at, expires_at + std::chrono::hours(2));
    REQUIRE(warning);
    CHECK(warning->id == "trust.document_expired");
    CHECK(warning->severity == Severity::Warning);
    const Arg* expired_for = warning->find_arg("expired_for");
    REQUIRE(expired_for);
    CHECK(std::get<std::chrono::milliseconds>(*expired_for) == std::chrono::hours(2));
}

TEST_CASE("key_id_of is the first 8 sha256 bytes of the raw key in lowercase hex") {
    const auto key = decode_hex<kEd25519PublicKeySize>(read_data("known_answer.pub"));
    REQUIRE(key);
    // Taken from the .sig the openssl CLI wrote with the same key.
    CHECK(key_id_of(*key) == "c4101c216ea76929");
}

TEST_CASE("KeyRing finds both slots by key id only") {
    const TestSigner current;
    const TestSigner next;
    const KeyRing ring(SignedDocumentKind::ReleaseManifest, {current.public_key(), next.public_key()});

    CHECK(ring.kind() == SignedDocumentKind::ReleaseManifest);
    REQUIRE(ring.find(key_id_of(current.public_key())));
    CHECK(*ring.find(key_id_of(current.public_key())) == current.public_key());
    REQUIRE(ring.find(key_id_of(next.public_key())));
    CHECK(*ring.find(key_id_of(next.public_key())) == next.public_key());
    CHECK_FALSE(ring.find(""));
    CHECK_FALSE(ring.find("0000000000000000"));

    const KeyRing next_only(SignedDocumentKind::ReleaseManifest, {{}, next.public_key()});
    CHECK_FALSE(next_only.find(key_id_of(current.public_key())));
    CHECK(next_only.find(key_id_of(next.public_key())));
}

TEST_CASE("the pinned ring never trusts a key generated at runtime") {
    const TestSigner signer;
    for (const auto kind : {SignedDocumentKind::BuildCatalog, SignedDocumentKind::ReleaseManifest}) {
        const KeyRing ring = KeyRing::pinned(kind);
        CHECK(ring.kind() == kind);
        const auto result = verify_signed(ring, signed_by(signer, kind, "{}"));
        REQUIRE_FALSE(result);
        CHECK(result.error().code == TrustErrorCode::UnknownKey);
        CHECK(result.error().document == kind);
    }
}

TEST_CASE("verify_signed rejects a changed signature and a swapped .sig") {
    const TestSigner signer;
    const KeyRing ring(SignedDocumentKind::BuildCatalog, {signer.public_key(), {}});

    SECTION("one flipped signature bit") {
        auto document = signed_by(signer, SignedDocumentKind::BuildCatalog, R"({"serial":1})");
        document.signature[10] ^= 0x01;
        const auto result = verify_signed(ring, document);
        REQUIRE_FALSE(result);
        CHECK(result.error().code == TrustErrorCode::SignatureInvalid);
        CHECK(result.error().key_id == key_id_of(signer.public_key()));
    }

    SECTION("the signature of another body under the same key") {
        const auto other = signed_by(signer, SignedDocumentKind::BuildCatalog, R"({"serial":2})");
        auto document = signed_by(signer, SignedDocumentKind::BuildCatalog, R"({"serial":1})");
        document.signature = other.signature;
        const auto result = verify_signed(ring, document);
        REQUIRE_FALSE(result);
        CHECK(result.error().code == TrustErrorCode::SignatureInvalid);
    }

    SECTION("an empty body") {
        CHECK(verify_signed(ring, signed_by(signer, SignedDocumentKind::BuildCatalog, "")));
    }
}

TEST_CASE("a CLI-signed manifest never verifies as a catalog") {
    const auto key = decode_hex<kEd25519PublicKeySize>(read_data("known_answer.pub"));
    REQUIRE(key);
    const KeyRing catalog_ring(SignedDocumentKind::BuildCatalog, {*key, {}});
    const auto document = make_signed_document(SignedDocumentKind::BuildCatalog, bytes(read_data("release_manifest.json")),
                                               read_data("release_manifest.json.sig"));
    REQUIRE(document);
    const auto result = verify_signed(catalog_ring, *document);
    REQUIRE_FALSE(result);
    CHECK(result.error().code == TrustErrorCode::SignatureInvalid);
}

TEST_CASE("SerialGuard never persists a rollback or a repeat") {
    int calls = 0;
    SerialGuard guard(SignedDocumentKind::BuildCatalog, 5, [&](u64) -> Result<void> {
        ++calls;
        return {};
    });

    CHECK_FALSE(guard.admit(0));
    CHECK_FALSE(guard.admit(4));
    CHECK(guard.admit(5) == SerialCheck::Same);
    CHECK(calls == 0);
    CHECK(guard.highest_seen() == 5);
}

TEST_CASE("SerialGuard admits a serial again once persisting recovers") {
    bool fail = true;
    std::vector<u64> persisted;
    SerialGuard guard(SignedDocumentKind::ReleaseManifest, 1, [&](u64 serial) -> Result<void> {
        persisted.push_back(serial);
        if (fail) return std::unexpected(Diagnostic{.domain = ErrorDomain::Storage, .id = "storage.write_failed"});
        return {};
    });

    const auto failed = guard.admit(2);
    REQUIRE_FALSE(failed);
    CHECK(failed.error().serial == 2);
    CHECK(failed.error().highest_seen == 1);
    CHECK(to_diagnostic(failed.error()).retryable);

    // The failed serial was never admitted, so the old one is not a rollback yet.
    fail = false;
    CHECK(guard.admit(1) == SerialCheck::Same);
    CHECK(guard.admit(2) == SerialCheck::Advanced);
    CHECK(persisted == std::vector<u64>{2, 2});
    CHECK(guard.highest_seen() == 2);
}

TEST_CASE("to_diagnostic maps every trust error") {
    const auto text_arg = [](const Diagnostic& diag, std::string_view name) {
        const Arg* arg = diag.find_arg(name);
        REQUIRE(arg);
        return std::get<std::string>(*arg);
    };

    SECTION("signature malformed") {
        const Diagnostic diag = to_diagnostic(
            TrustError{.code = TrustErrorCode::SignatureMalformed, .document = SignedDocumentKind::ReleaseManifest});
        CHECK(diag.domain == ErrorDomain::Trust);
        CHECK(diag.id == "trust.signature_malformed");
        CHECK(text_arg(diag, "document") == "release_manifest");
        CHECK_FALSE(diag.retryable);
    }

    SECTION("unknown key") {
        const Diagnostic diag =
            to_diagnostic(TrustError{.code = TrustErrorCode::UnknownKey, .key_id = "0123456789abcdef"});
        CHECK(diag.id == "trust.unknown_key");
        CHECK(text_arg(diag, "document") == "build_catalog");
        CHECK(text_arg(diag, "key_id") == "0123456789abcdef");
    }

    SECTION("serial rollback") {
        const Diagnostic diag =
            to_diagnostic(TrustError{.code = TrustErrorCode::SerialRollback, .serial = 3, .highest_seen = 9});
        CHECK(diag.kind == ErrorKind::Conflict);
        REQUIRE(diag.find_arg("serial"));
        CHECK(std::get<u64>(*diag.find_arg("serial")) == 3);
        REQUIRE(diag.find_arg("highest_seen"));
        CHECK(std::get<u64>(*diag.find_arg("highest_seen")) == 9);
    }

    SECTION("serial persist failed without a cause") {
        const Diagnostic diag = to_diagnostic(TrustError{.code = TrustErrorCode::SerialPersistFailed, .serial = 4});
        CHECK(diag.id == "trust.serial_persist_failed");
        CHECK(diag.retryable);
        CHECK(diag.causes.empty());
    }

    SECTION("crypto failure carries OpenSSL's reason") {
        const Diagnostic with = to_diagnostic(TrustError{.code = TrustErrorCode::CryptoFailure, .detail = "bad"});
        CHECK(with.id == "trust.crypto_failure");
        CHECK(with.detail == "bad");
        const Diagnostic without = to_diagnostic(TrustError{.code = TrustErrorCode::CryptoFailure});
        CHECK_FALSE(without.detail);
    }
}

TEST_CASE("check_expiry names the document and warns one millisecond late") {
    const auto expires_at = std::chrono::system_clock::time_point{} + std::chrono::hours(1);

    CHECK_FALSE(check_expiry(SignedDocumentKind::ReleaseManifest, expires_at, expires_at - std::chrono::hours(1)));
    const auto warning =
        check_expiry(SignedDocumentKind::ReleaseManifest, expires_at, expires_at + std::chrono::milliseconds(1));
    REQUIRE(warning);
    CHECK(warning->domain == ErrorDomain::Trust);
    const Arg* document = warning->find_arg("document");
    REQUIRE(document);
    CHECK(std::get<std::string>(*document) == "release_manifest");
}

TEST_CASE("check_expiry measures time points centuries apart") {
    const auto now = std::chrono::system_clock::time_point{} + std::chrono::hours(1);
    const auto warning = check_expiry(SignedDocumentKind::BuildCatalog, std::chrono::system_clock::time_point::min(), now);
    REQUIRE(warning);
    const Arg* expired_for = warning->find_arg("expired_for");
    REQUIRE(expired_for);
    CHECK(std::get<std::chrono::milliseconds>(*expired_for) > std::chrono::hours(24 * 365 * 200));
}

TEST_CASE("check_expiry never reports a zero expiry age") {
    const auto expires_at = std::chrono::system_clock::time_point{} + std::chrono::hours(1);
    const auto warning = check_expiry(SignedDocumentKind::BuildCatalog, expires_at,
                                      expires_at + std::chrono::system_clock::duration(1));
    REQUIRE(warning);
    CHECK(std::get<std::chrono::milliseconds>(*warning->find_arg("expired_for")) == std::chrono::milliseconds(1));
}
