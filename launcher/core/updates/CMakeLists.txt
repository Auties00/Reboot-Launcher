# Public headers name storage's DocumentStore, whose Document constraint needs the complete documents.
reboot_add_package(updates
  SOURCES
    src/apply_gate.cpp
    src/messages.cpp
    src/pending_update_marker.cpp
    src/resume_record.cpp
    src/update_event.cpp
    src/update_offer.cpp
    src/update_service.cpp
  DEPS reboot::net Boost::json
  PUBLIC_DEPS reboot::components reboot::storage)

# The service tests sign their own release manifests for the real ManifestService.
if(TARGET reboot_updates_tests)
  file(GLOB updates_test_files CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/tests/*.cpp" "${CMAKE_CURRENT_SOURCE_DIR}/tests/*.hpp")
  _reboot_record_target(reboot_updates_tests test
    DEPS reboot::updates reboot::components reboot::storage reboot::net reboot::trust reboot::testing
    SOURCES ${updates_test_files})
  target_link_libraries(reboot_updates_tests PRIVATE reboot::trust OpenSSL::Crypto)
endif()
