# The lean header-only subset: value types, diagnostic data, versions, paths, framing, secrets and
# UniqueFunction. The payload (winhost, the client DLL), the game server and the SDK build on it
# alone, so it has no sources, no message registry and no Asio.
add_library(reboot_base INTERFACE)
add_library(reboot::base ALIAS reboot_base)
target_include_directories(reboot_base INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_link_libraries(reboot_base INTERFACE sb_contract Boost::pfr)
_reboot_record_target(reboot_base foundation)
