reboot_add_package(browser
  SOURCES
    src/messages.cpp
    src/version_match.cpp
  PUBLIC_DEPS reboot::net reboot::components)

if(TARGET reboot_browser_tests)
  target_compile_definitions(reboot_browser_tests PRIVATE REBOOT_BROWSER_TEST_DATA="${CMAKE_CURRENT_SOURCE_DIR}/tests/data")
endif()
