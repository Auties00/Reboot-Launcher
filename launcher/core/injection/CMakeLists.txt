reboot_add_package(injection
  SOURCES
    src/dll_path_validator.cpp
    src/inject_failed.cpp
    src/injection_plan.cpp
    src/legacy_requirements.cpp
    src/messages.cpp)

if(TARGET reboot_injection_tests)
  target_compile_definitions(reboot_injection_tests PRIVATE REBOOT_INJECTION_TEST_DATA="${CMAKE_CURRENT_SOURCE_DIR}/tests/data")
endif()
