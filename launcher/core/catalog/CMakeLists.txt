reboot_add_package(catalog
  SOURCES
    src/alias_table.cpp
    src/bundled_catalog_source.cpp
    src/catalog.cpp
    src/catalog_error.cpp
    src/catalog_service.cpp
    src/messages.cpp
    src/parse_catalog.cpp
    src/signed_remote_catalog_source.cpp
    src/verified_catalog.cpp
  DEPS reboot::trust reboot::net Boost::json)

if(TARGET reboot_catalog_tests)
  target_compile_definitions(reboot_catalog_tests PRIVATE REBOOT_CATALOG_TEST_DATA="${CMAKE_CURRENT_SOURCE_DIR}/tests/data")
  # The tests sign their own catalogs.
  target_link_libraries(reboot_catalog_tests PRIVATE OpenSSL::Crypto)
endif()
