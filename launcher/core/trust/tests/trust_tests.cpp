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

using namespace reboot;
using namespace reboot::trust;

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
                                       "ed25519 0123456789ABCDEF " + signature}) {
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
