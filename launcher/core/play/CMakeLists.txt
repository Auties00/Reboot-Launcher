# Public headers carry the session's pinned inputs; front and host are only forward-declared there.
reboot_add_package(play
  SOURCES
    src/messages.cpp
  DEPS reboot::front reboot::host
  PUBLIC_DEPS
    reboot::storage reboot::builds reboot::catalog reboot::support reboot::components reboot::compat
    reboot::injection reboot::game_channel reboot::identity reboot::secrets reboot::backend reboot::process
    reboot::sessions reboot::browser)
