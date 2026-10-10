# Linux adapters for every engine port, and ports::make_platform().
# Depends on: posix.
if(NOT CMAKE_SYSTEM_NAME STREQUAL "Linux")
  return()
endif()

# libsecret and libgio are loaded with dlopen at run time, so neither is a link dependency.
reboot_add_package(os_linux/platform
  OS LINUX
  SOURCES
    src/base64.cpp
    src/child_reaping.cpp
    src/desktop_files.cpp
    src/file_uri.cpp
    src/glib_runtime.cpp
    src/helper_process.cpp
    src/inet_diag.cpp
    src/inotify_events.cpp
    src/inotify_watcher.cpp
    src/libsecret_secret_store.cpp
    src/linux_disk_info.cpp
    src/linux_file_system.cpp
    src/linux_integration_registrar.cpp
    src/linux_log_file_system.cpp
    src/linux_prerequisite_probe.cpp
    src/linux_resolver.cpp
    src/linux_system_info.cpp
    src/linux_update_applier.cpp
    src/make_platform.cpp
    src/messages.cpp
    src/mount_table.cpp
    src/os_release.cpp
    src/passwd_entry.cpp
    src/pidfd_process_launcher.cpp
    src/proc_stat.cpp
    src/process_environment.cpp
    src/sock_diag_peer_inspector.cpp
    src/sock_diag_port_inspector.cpp
    src/socket_owner.cpp
    src/socket_table.cpp
    src/systemd_units.cpp
    src/systemd_user.cpp
    src/tarball_layout.cpp
    src/text_files.cpp
    src/update_entries.cpp
    src/update_package.cpp
    src/xdg_dirs.cpp
    src/xdg_paths.cpp
    src/xdg_shell.cpp
  PUBLIC_DEPS
    reboot::posix
  DEPS
    LibArchive::LibArchive
    ${CMAKE_DL_LIBS})

# Real-adapter suite (atomic replace, OFD lock exclusivity, inotify delivery, pidfd tree and
# scope kill, sock_diag owners, update extraction); runs on the Linux CI runner only. It writes
# its update packages with libarchive.
if(REBOOT_BUILD_TESTS)
  file(GLOB conformance_sources CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/conformance/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/conformance/*.hpp")
  if(conformance_sources)
    add_executable(reboot_os_linux_conformance ${conformance_sources})
    _reboot_record_target(reboot_os_linux_conformance test DEPS reboot::os_linux_platform reboot::testing
      SOURCES ${conformance_sources})
    target_include_directories(reboot_os_linux_conformance PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
    target_link_libraries(reboot_os_linux_conformance PRIVATE
      reboot_os_linux_platform LibArchive::LibArchive Catch2::Catch2WithMain reboot_warnings
      $<TARGET_NAME_IF_EXISTS:reboot_testing>)
    if(NOT COMMAND catch_discover_tests)
      include(Catch)
    endif()
    catch_discover_tests(reboot_os_linux_conformance)
  endif()
endif()
