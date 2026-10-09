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
    src/resumable_downloader.cpp
    src/udp_beacon_prober.cpp
    src/url.cpp
  DEPS CURL::libcurl MsQuic::MsQuic miniupnpc::miniupnpc libnatpmp::natpmp
  PUBLIC_DEPS reboot::storage)

# macOS verifies TLS through the keychain (USE_APPLE_SECTRUST), which curl has since 8.17.
if(APPLE AND CURL_VERSION VERSION_LESS 8.17)
  message(FATAL_ERROR "core/net needs curl 8.17 or later on macOS for USE_APPLE_SECTRUST, found ${CURL_VERSION}")
endif()
