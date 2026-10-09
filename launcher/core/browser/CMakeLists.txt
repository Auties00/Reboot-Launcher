reboot_add_package(browser
  SOURCES
    src/browse_choices.cpp
    src/browser_session.cpp
    src/connection_state.cpp
    src/deep_link.cpp
    src/deep_link_service.cpp
    src/full_jitter_backoff.cpp
    src/game_server_target.cpp
    src/join_outcome.cpp
    src/join_service.cpp
    src/list_state.cpp
    src/messages.cpp
    src/rbsb_endpoint.cpp
    src/rbsb_request_error.cpp
    src/search.cpp
    src/server_list.cpp
    src/server_row.cpp
    src/token_bucket.cpp
    src/version_match.cpp
    src/view_spec.cpp
  PUBLIC_DEPS reboot::net reboot::components)

if(TARGET reboot_browser_tests)
  target_compile_definitions(reboot_browser_tests PRIVATE REBOOT_BROWSER_TEST_DATA="${CMAKE_CURRENT_SOURCE_DIR}/tests/data")
endif()
