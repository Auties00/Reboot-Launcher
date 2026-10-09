reboot_add_package(front
  SOURCES
    src/asio_address.cpp
    src/authority.cpp
    src/content_decoding.cpp
    src/form_body.cpp
    src/front_connection.cpp
    src/front_core.cpp
    src/front_path.cpp
    src/legacy_fixed_listeners.cpp
    src/loopback_authority.cpp
    src/messages.cpp
    src/peer_user.cpp
    src/proxy_headers.cpp
    src/route_table.cpp
    src/session_front.cpp
    src/session_key.cpp
    src/strand_routes.cpp
    src/ticket_exchange.cpp
    src/tls_client.cpp
    src/upstream_origin.cpp
    src/upstream_policy.cpp
  DEPS Boost::beast Boost::json OpenSSL::SSL
  PUBLIC_DEPS reboot::net)

if(TARGET reboot_front_tests)
  target_compile_definitions(reboot_front_tests PRIVATE REBOOT_FRONT_TEST_DATA="${CMAKE_CURRENT_SOURCE_DIR}/tests/data")
endif()

# Beast's coroutine relay instantiates more sections than a default COFF object holds.
if(MSVC)
  target_compile_options(reboot_front PRIVATE /bigobj)
  if(TARGET reboot_front_tests)
    target_compile_options(reboot_front_tests PRIVATE /bigobj)
  endif()
endif()
