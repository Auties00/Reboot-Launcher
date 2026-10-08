# Public headers use storage's LibraryDocument store, catalog's archive enums and aliases, and support's roles.
reboot_add_package(builds
  SOURCES
    src/cl_table.cpp
    src/derive_version.cpp
    src/messages.cpp
    src/release_marker.cpp
  DEPS reboot::net
  PUBLIC_DEPS reboot::storage reboot::catalog reboot::support)

# LibArchiveExtractor; no public header includes archive.h.
target_link_libraries(reboot_builds PRIVATE LibArchive::LibArchive)

if(TARGET reboot_builds_tests)
  target_compile_definitions(reboot_builds_tests
    PRIVATE REBOOT_BUILDS_TEST_DATA="${CMAKE_CURRENT_SOURCE_DIR}/tests/data")
endif()
