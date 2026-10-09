# Private engine IPC, shared by reboot-engine and reboot_client; it uses no Boost.Asio, no API
# types and no domain package.
reboot_add_package(ipc
  SOURCES
    src/endpoint.cpp
    src/ipc_client.cpp
    src/ipc_codec.cpp
    src/ipc_connection.cpp
    src/ipc_server.cpp
    src/messages.cpp
    src/outbound_budget.cpp)
