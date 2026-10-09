# LegacyOutputAdapter reads lines through process::LineReader; liveness follows process::LivenessPolicy.
reboot_add_package(game_channel
  SOURCES
    src/channel_core.cpp
    src/client_dll_peer.cpp
    src/control_token.cpp
    src/game_channel_error.cpp
    src/game_channel_listener.cpp
    src/legacy_output_adapter.cpp
    src/lifecycle_markers.cpp
    src/messages.cpp
    src/peer_link.cpp
    src/tcp_byte_stream.cpp
    src/token_registry.cpp
    src/ue_log_tail.cpp
    src/winhost_peer.cpp
  DEPS reboot::process)
