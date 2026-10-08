# The deterministic test kit every package's tests link: fakes, contract peers, conformance suites,
# chaos helpers, the fake executables and the fuzz targets.
if(NOT REBOOT_BUILD_TESTS AND NOT REBOOT_BUILD_FAKES)
  return()
endif()

# reboot::api is public because ApiTestClient's header speaks reboot.api.v1.
reboot_add_package(testing
  SOURCES
    src/conformance_report.cpp
    src/deterministic_runtime.cpp
    src/fake_caller_context.cpp
    src/fake_disk_info.cpp
    src/fake_engine_starter.cpp
    src/fake_executables.cpp
    src/fake_loopback_peer_inspector.cpp
    src/fake_platform_paths.cpp
    src/fake_port_binder.cpp
    src/fake_port_inspector.cpp
    src/fake_prereqs.cpp
    src/fake_random.cpp
    src/fake_registrar.cpp
    src/fake_runner_platform.cpp
    src/fake_secret_store.cpp
    src/fake_security_probe.cpp
    src/fake_shell.cpp
    src/fake_system_info.cpp
    src/fake_update_applier.cpp
    src/frame_log.cpp
    src/fuzz.cpp
    src/golden.cpp
    src/manual_waiter.cpp
    src/messages.cpp
    src/port_conformance.cpp
    src/wall_clock_waiter.cpp
  DEPS Boost::json
  PUBLIC_DEPS reboot::api)

if(REBOOT_BUILD_FAKES)
  # Phase 1 lands the peers one by one; each executable builds once its peer's body exists.
  foreach(fake IN ITEMS backend game_server game)
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/src/fake_${fake}.cpp")
      target_sources(reboot_testing PRIVATE src/fake_${fake}.cpp)
      add_subdirectory(fake_${fake})
    endif()
  endforeach()

  # fake_executables.cpp answers from these.
  if(TARGET reboot-fake-backend)
    target_compile_definitions(reboot_testing PRIVATE REBOOT_FAKE_BACKEND_EXE="$<TARGET_FILE:reboot-fake-backend>")
  endif()
  if(TARGET reboot-fake-game-server)
    target_compile_definitions(reboot_testing PRIVATE
      REBOOT_FAKE_GAME_SERVER_EXE="$<TARGET_FILE:reboot-fake-game-server>")
  endif()
  # Off Windows the Wine kill-chain test needs a Windows-built reboot-fake-game.exe.
  set(REBOOT_FAKE_GAME_EXE "" CACHE FILEPATH "A Windows-built reboot-fake-game.exe, for Wine runs off Windows")
  if(TARGET reboot-fake-game)
    target_compile_definitions(reboot_testing PRIVATE REBOOT_FAKE_GAME_EXE="$<TARGET_FILE:reboot-fake-game>")
  elseif(REBOOT_FAKE_GAME_EXE)
    target_compile_definitions(reboot_testing PRIVATE REBOOT_FAKE_GAME_EXE="${REBOOT_FAKE_GAME_EXE}")
  endif()
endif()

if(REBOOT_BUILD_TESTS)
  add_subdirectory(fuzz)
endif()
