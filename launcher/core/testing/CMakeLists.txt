# The deterministic test kit every package's tests link: fakes, contract peers, conformance suites,
# chaos helpers, the fake executables and the fuzz targets.
if(NOT REBOOT_BUILD_TESTS AND NOT REBOOT_BUILD_FAKES)
  return()
endif()

# reboot::api is public because ApiTestClient's header speaks reboot.api.v1.
reboot_add_package(testing
  SOURCES
    src/api_test_client.cpp
    src/chaos.cpp
    src/conformance_report.cpp
    src/contract_conformance.cpp
    src/current_process.cpp
    src/deterministic_runtime.cpp
    src/event_recorder.cpp
    src/fake_backend.cpp
    src/fake_caller_context.cpp
    src/fake_client_dll.cpp
    src/fake_disk_info.cpp
    src/fake_engine_starter.cpp
    src/fake_executables.cpp
    src/fake_file_watcher.cpp
    src/fake_game.cpp
    src/fake_game_server.cpp
    src/fake_http_transport.cpp
    src/fake_loopback_peer_inspector.cpp
    src/fake_platform.cpp
    src/fake_platform_paths.cpp
    src/fake_port_binder.cpp
    src/fake_port_inspector.cpp
    src/fake_prereqs.cpp
    src/fake_quic.cpp
    src/fake_random.cpp
    src/fake_registrar.cpp
    src/fake_resolver.cpp
    src/fake_runner_platform.cpp
    src/fake_scripts.cpp
    src/fake_secret_store.cpp
    src/fake_security_probe.cpp
    src/fake_session_host.cpp
    src/fake_shell.cpp
    src/fake_system_info.cpp
    src/fake_update_applier.cpp
    src/fake_winhost.cpp
    src/frame_log.cpp
    src/fuzz.cpp
    src/game_control_bootstrap.cpp
    src/golden.cpp
    src/in_memory_file_system.cpp
    src/in_memory_ipc.cpp
    src/in_memory_log_file_system.cpp
    src/manual_waiter.cpp
    src/memory_stream_pair.cpp
    src/messages.cpp
    src/peer_process.cpp
    src/port_conformance.cpp
    src/port_conformance_io.cpp
    src/raw_stdio.cpp
    src/scratch_dir.cpp
    src/scripted_child.cpp
    src/scripted_process_launcher.cpp
    src/socket_port_binder.cpp
    src/stdio_peer_core.cpp
    src/tcp_stream.cpp
    src/wall_clock_waiter.cpp
    src/win_modules.cpp
  DEPS Boost::json
  PUBLIC_DEPS reboot::api)

if(REBOOT_BUILD_FAKES)
  add_subdirectory(fake_backend)
  add_subdirectory(fake_game_server)
  add_subdirectory(fake_game)

  # fake_executables.cpp answers from these.
  target_compile_definitions(reboot_testing PRIVATE
    REBOOT_FAKE_BACKEND_EXE="$<TARGET_FILE:reboot-fake-backend>"
    REBOOT_FAKE_GAME_SERVER_EXE="$<TARGET_FILE:reboot-fake-game-server>")
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
