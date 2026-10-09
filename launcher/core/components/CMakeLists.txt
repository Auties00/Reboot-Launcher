reboot_add_package(components
  SOURCES
    src/archive_extract.cpp
    src/bundled_assets.cpp
    src/component_pin.cpp
    src/component_problem.cpp
    src/component_store.cpp
    src/deletion_guard.cpp
    src/integrity_hold.cpp
    src/json_fields.cpp
    src/manifest_service.cpp
    src/messages.cpp
    src/payload_entry.cpp
    src/release_manifest.cpp
    src/store_index.cpp
  DEPS reboot::trust reboot::net reboot::storage Boost::json)

# Runtime archives are unpacked in process.
target_link_libraries(reboot_components PRIVATE LibArchive::LibArchive)

if(TARGET reboot_components_tests)
  # The tests sign manifests and build runtime archives themselves.
  target_link_libraries(reboot_components_tests PRIVATE LibArchive::LibArchive OpenSSL::Crypto)
  target_compile_definitions(reboot_components_tests PRIVATE REBOOT_COMPONENTS_TEST_DATA="${CMAKE_CURRENT_SOURCE_DIR}/tests/data")
endif()
