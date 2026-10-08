# Header-only, so /MT executables (winhost, win32session) and the phase-2/3 programs can use it
# without Boost.Asio. It uses only the header-only parts of the foundation.
add_library(reboot_contracts INTERFACE)
add_library(reboot::contracts ALIAS reboot_contracts)
target_include_directories(reboot_contracts INTERFACE
  "${CMAKE_CURRENT_SOURCE_DIR}/include"
  "${CMAKE_CURRENT_SOURCE_DIR}/../foundation/include")
target_link_libraries(reboot_contracts INTERFACE sb_contract Boost::pfr)
_reboot_record_target(reboot_contracts foundation)
