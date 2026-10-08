reboot_add_package(net
  SOURCES
    src/messages.cpp
  DEPS CURL::libcurl MsQuic::MsQuic miniupnpc::miniupnpc libnatpmp::natpmp
  PUBLIC_DEPS reboot::storage)

# macOS verifies TLS through the keychain (USE_APPLE_SECTRUST), which curl has since 8.17.
if(APPLE AND CURL_VERSION VERSION_LESS 8.17)
  message(FATAL_ERROR "core/net needs curl 8.17 or later on macOS for USE_APPLE_SECTRUST, found ${CURL_VERSION}")
endif()
