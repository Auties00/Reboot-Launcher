# Raw Ed25519 public keys as 64 hex digits; empty pins nothing, so every document is rejected.
set(REBOOT_TRUST_CATALOG_KEY_CURRENT "" CACHE STRING "Build catalog signing key (current)")
set(REBOOT_TRUST_CATALOG_KEY_NEXT "" CACHE STRING "Build catalog signing key (next)")
set(REBOOT_TRUST_MANIFEST_KEY_CURRENT "" CACHE STRING "Release manifest signing key (current)")
set(REBOOT_TRUST_MANIFEST_KEY_NEXT "" CACHE STRING "Release manifest signing key (next)")
option(REBOOT_TRUST_REQUIRE_PINNED_KEYS "Fail configure unless both current keys are pinned (release jobs)" OFF)

if(REBOOT_TRUST_REQUIRE_PINNED_KEYS AND (REBOOT_TRUST_CATALOG_KEY_CURRENT STREQUAL "" OR REBOOT_TRUST_MANIFEST_KEY_CURRENT STREQUAL ""))
  message(FATAL_ERROR "REBOOT_TRUST_REQUIRE_PINNED_KEYS is ON but a current catalog or manifest key is empty")
endif()

reboot_add_package(trust
  SOURCES
    src/check_expiry.cpp
    src/key_ring.cpp
    src/messages.cpp
    src/pinned_keys.cpp
    src/serial_guard.cpp
    src/signed_document.cpp
    src/signed_document_kind.cpp
    src/trust_error.cpp
    src/verify_signed.cpp
  DEPS OpenSSL::Crypto)

set_source_files_properties(src/pinned_keys.cpp PROPERTIES COMPILE_DEFINITIONS
  "REBOOT_TRUST_CATALOG_KEY_CURRENT=\"${REBOOT_TRUST_CATALOG_KEY_CURRENT}\";REBOOT_TRUST_CATALOG_KEY_NEXT=\"${REBOOT_TRUST_CATALOG_KEY_NEXT}\";REBOOT_TRUST_MANIFEST_KEY_CURRENT=\"${REBOOT_TRUST_MANIFEST_KEY_CURRENT}\";REBOOT_TRUST_MANIFEST_KEY_NEXT=\"${REBOOT_TRUST_MANIFEST_KEY_NEXT}\"")

if(TARGET reboot_trust_tests)
  target_compile_definitions(reboot_trust_tests PRIVATE REBOOT_TRUST_TEST_DATA="${CMAKE_CURRENT_SOURCE_DIR}/tests/data")
endif()
