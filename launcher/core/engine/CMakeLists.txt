# The composition root: every domain package is a dependency. Public headers carry api, ipc, play,
# sessions, updates, process and the store documents of storage, compat, gameserver and host.
reboot_add_package(engine
  SOURCES
    src/engine_command_line.cpp
    src/messages.cpp
  DEPS
    reboot::logging reboot::trust reboot::ux reboot::net reboot::components reboot::catalog
    reboot::support reboot::builds reboot::secrets reboot::identity reboot::injection
    reboot::game_channel reboot::backend reboot::front reboot::browser reboot::publish
    reboot::integration
  PUBLIC_DEPS
    reboot::api reboot::ipc reboot::play reboot::sessions reboot::updates reboot::process
    reboot::storage reboot::compat reboot::gameserver reboot::host)

# The OS module supplies make_platform and make_client_platform.
reboot_add_executable(reboot-engine
  SOURCES main.cpp
  DEPS reboot::engine reboot::platform)

# A console window would live as long as the engine, and closing it would kill the engine.
if(WIN32)
  set_target_properties(reboot-engine PROPERTIES WIN32_EXECUTABLE TRUE)
  if(MSVC)
    target_link_options(reboot-engine PRIVATE /ENTRY:mainCRTStartup)
  endif()
endif()
