reboot_add_package(identity
  SOURCES
    src/account_record.cpp
    src/display_name.cpp
    src/legacy_login.cpp
    src/login_plan.cpp
    src/messages.cpp
    src/random_chars.cpp
  PUBLIC_DEPS reboot::storage)

if(TARGET reboot_identity_tests)
  target_compile_definitions(reboot_identity_tests PRIVATE REBOOT_IDENTITY_TEST_DATA="${CMAKE_CURRENT_SOURCE_DIR}/tests/data")
endif()
