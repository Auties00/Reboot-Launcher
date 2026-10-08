reboot_add_package(front
  SOURCES
    src/authority.cpp
    src/form_body.cpp
    src/front_path.cpp
    src/loopback_authority.cpp
    src/messages.cpp
    src/peer_user.cpp
    src/session_key.cpp
    src/ticket_exchange.cpp
    src/upstream_origin.cpp
    src/upstream_policy.cpp
  DEPS Boost::beast Boost::json OpenSSL::SSL
  PUBLIC_DEPS reboot::net)

if(TARGET reboot_front_tests)
  target_compile_definitions(reboot_front_tests PRIVATE REBOOT_FRONT_TEST_DATA="${CMAKE_CURRENT_SOURCE_DIR}/tests/data")
endif()
