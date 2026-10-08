reboot_add_package(logging
  SOURCES
    src/error_router.cpp
    src/error_router_watch.cpp
    src/log_file_names.cpp
    src/log_files_in_use.cpp
    src/log_filter.cpp
    src/log_format.cpp
    src/log_line_forwarder.cpp
    src/log_ring.cpp
    src/messages.cpp
    src/retention_policy.cpp
    src/wine_log_files.cpp)

# LogExporter writes zip archives.
target_link_libraries(reboot_logging PRIVATE LibArchive::LibArchive)

if(TARGET reboot_logging_tests)
  target_compile_definitions(reboot_logging_tests PRIVATE REBOOT_LOGGING_TEST_DATA="${CMAKE_CURRENT_SOURCE_DIR}/tests/data")
  # Reads the redaction canary golden.
  target_link_libraries(reboot_logging_tests PRIVATE Boost::json)
endif()
