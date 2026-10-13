# Header-only on reboot::base alone, so /MT executables (winhost, win32session) and the backend and
# game-server programs can use it without the foundation runtime.
add_library(reboot_contracts INTERFACE)
add_library(reboot::contracts ALIAS reboot_contracts)
target_include_directories(reboot_contracts INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_link_libraries(reboot_contracts INTERFACE reboot::base)
_reboot_record_target(reboot_contracts foundation)
