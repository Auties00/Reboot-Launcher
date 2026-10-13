reboot_add_package(net
  SOURCES
    src/address_resolver.cpp
    src/asio_datagram_connector.cpp
    src/curl_http_transport.cpp
    src/download_support.cpp
    src/errors.cpp
    src/gateway_codes.cpp
    src/gateway_diagnostic.cpp
    src/host_tls_memory.cpp
    src/http_client.cpp
    src/messages.cpp
    src/miniupnpc_gateway.cpp
    src/msquic_transport.cpp
    src/natpmp_gateway.cpp
    src/port_mapper_service.cpp
    src/port_owner_service.cpp
    src/port_preflight.cpp
    src/quic_peer_verifier.cpp
    src/redirect_guard.cpp
    src/resumable_downloader.cpp
    src/udp_beacon_prober.cpp
    src/url.cpp
  DEPS Boost::asio CURL::libcurl MsQuic::MsQuic miniupnpc::miniupnpc libnatpmp::natpmp OpenSSL::Crypto
  PUBLIC_DEPS reboot::storage
  # Real MsQuic and curl TLS handshakes against a local server, and real UDP ports.
  SERIAL_TAGS quic tls ports)

if(TARGET reboot_net_tests)
  # The TLS tests make their own CA and serve https and QUIC with it.
  target_link_libraries(reboot_net_tests PRIVATE OpenSSL::SSL OpenSSL::Crypto)
endif()

# macOS verifies TLS through the keychain (USE_APPLE_SECTRUST), which curl has since 8.17.
if(APPLE AND CURL_VERSION VERSION_LESS 8.17)
  message(FATAL_ERROR "core/net needs curl 8.17 or later on macOS for USE_APPLE_SECTRUST, found ${CURL_VERSION}")
endif()
