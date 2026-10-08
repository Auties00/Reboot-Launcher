# Private engine IPC, shared by reboot-engine and reboot_client; it uses no Boost.Asio, no API
# types and no domain package.
reboot_add_package(ipc
  SOURCES
    src/messages.cpp)
