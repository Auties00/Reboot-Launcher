# Stands where the game's shipping exe stands, so x64 like the game. Linux CI runs this Windows
# build under the pinned Wine with the real reboot-winhost.exe.
reboot_add_executable(reboot-fake-game
  OS WINDOWS
  SOURCES main.cpp
  DEPS reboot::testing)
