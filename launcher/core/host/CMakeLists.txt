# Public headers use gameserver, sessions, publish, net, support, storage and process types;
# HostServiceDeps names process::ChildRecordCallback.
reboot_add_package(host
  SOURCES
    src/host_error.cpp
    src/host_port_allocator.cpp
    src/host_profile.cpp
    src/host_profile_store.cpp
    src/host_profiles_document.cpp
    src/host_service.cpp
    src/host_start_request.cpp
    src/ip_cidr.cpp
    src/match_end_policy.cpp
    src/messages.cpp
    src/operator_policy.cpp
    src/port_block.cpp
    src/port_policy.cpp
  DEPS reboot::builds reboot::identity
  PUBLIC_DEPS reboot::storage reboot::support reboot::net reboot::gameserver reboot::sessions reboot::publish
              reboot::process)

# The service tests run a real builds::Library, which takes a catalog::CatalogService.
if(TARGET reboot_host_tests)
  file(GLOB host_test_files CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/tests/*.cpp" "${CMAKE_CURRENT_SOURCE_DIR}/tests/*.hpp")
  _reboot_record_target(reboot_host_tests test
    DEPS reboot::host reboot::builds reboot::catalog reboot::identity reboot::storage reboot::support reboot::net
         reboot::gameserver reboot::sessions reboot::publish reboot::process reboot::testing
    SOURCES ${host_test_files})
endif()
