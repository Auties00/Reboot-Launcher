# LegacyOutputAdapter reads lines through process::LineReader; liveness follows process::LivenessPolicy.
reboot_add_package(game_channel
  SOURCES
    src/lifecycle_markers.cpp
    src/messages.cpp
  DEPS reboot::process)
