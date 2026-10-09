# Public headers use storage's LibraryDocument store, catalog's archive enums and aliases, and support's roles.
reboot_add_package(builds
  SOURCES
    src/archive_probe.cpp
    src/build_installer.cpp
    src/build_support.cpp
    src/builds_error.cpp
    src/byte_sources.cpp
    src/cl_table.cpp
    src/derive_version.cpp
    src/detect_version.cpp
    src/file_finder.cpp
    src/import_service.cpp
    src/layout_resolver.cpp
    src/libarchive_extractor.cpp
    src/library.cpp
    src/messages.cpp
    src/path_text.cpp
    src/pe_version_reader.cpp
    src/release_marker.cpp
  DEPS reboot::net
  PUBLIC_DEPS reboot::storage reboot::catalog reboot::support)

# LibArchiveExtractor; no public header includes archive.h.
target_link_libraries(reboot_builds PRIVATE LibArchive::LibArchive)

if(TARGET reboot_builds_tests)
  target_compile_definitions(reboot_builds_tests
    PRIVATE REBOOT_BUILDS_TEST_DATA="${CMAKE_CURRENT_SOURCE_DIR}/tests/data")
  # The archive tests write their ZIP and 7z fixtures with libarchive.
  target_link_libraries(reboot_builds_tests PRIVATE LibArchive::LibArchive)
endif()
