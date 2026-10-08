reboot_add_package(storage
  SOURCES
    src/accounts_document.cpp
    src/backend_target.cpp
    src/console_key.cpp
    src/data_root.cpp
    src/document_file.cpp
    src/flush_waiters.cpp
    src/frontend_state_store.cpp
    src/json_text.cpp
    src/json_values.cpp
    src/key.cpp
    src/library_document.cpp
    src/load_report.cpp
    src/member_reader.cpp
    src/messages.cpp
    src/reset_service.cpp
    src/resume_document.cpp
    src/runtime_document.cpp
    src/settings.cpp
    src/settings_document.cpp
    src/settings_keys.cpp
    src/settings_registry.cpp
    src/shell_name.cpp
    src/state_document.cpp
  PUBLIC_DEPS
    Boost::json)

if(TARGET reboot_storage_tests)
  target_compile_definitions(reboot_storage_tests PRIVATE REBOOT_STORAGE_TEST_DATA="${CMAKE_CURRENT_SOURCE_DIR}/tests/data")
endif()
