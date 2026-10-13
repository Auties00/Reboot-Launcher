# Public headers carry the session's pinned inputs; front, host and gameserver are only forward-declared there.
reboot_add_package(play
  SOURCES
    src/deps_env.cpp
    src/display_context.cpp
    src/game_arg.cpp
    src/launch_args.cpp
    src/launch_plan.cpp
    src/match_targets.cpp
    src/messages.cpp
    src/play_core.cpp
    src/play_service.cpp
    src/play_session_driver.cpp
    src/play_session_state.cpp
  DEPS reboot::front reboot::host reboot::gameserver
  PUBLIC_DEPS
    reboot::storage reboot::builds reboot::catalog reboot::support reboot::components reboot::compat
    reboot::injection reboot::game_channel reboot::identity reboot::secrets reboot::backend reboot::process
    reboot::sessions reboot::browser)

# The tests run an io_context of their own.
if(TARGET reboot_play_tests)
  target_link_libraries(reboot_play_tests PRIVATE Boost::asio)
endif()
