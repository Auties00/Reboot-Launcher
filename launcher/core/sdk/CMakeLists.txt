# rb::sdk: the header-only C++ interface every C++ client (the WinUI app, the CLI, a Linux GUI)
# compiles with its own toolchain and CRT. It reaches reboot_client only through reboot/client.h, plus
# the api types and base; methods.hpp and event_data.hpp come from protoc-gen-reboot-cpp and
# english_catalog.hpp from reboot-l10n render.
add_library(reboot_sdk INTERFACE)
add_library(reboot::sdk ALIAS reboot_sdk)
target_include_directories(reboot_sdk INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_link_libraries(reboot_sdk INTERFACE reboot::api reboot_client Threads::Threads)
_reboot_record_target(reboot_sdk lean DEPS reboot::api)

# `cmake --install <build> --component sdk` lays out everything a client builds against: these
# headers, the api types and base, reboot/client.h, the sb codec headers they include, Boost.PFR and
# reboot_client itself. CI builds core/sdk/consumer against that alone.
set(sb_source "${PROJECT_SOURCE_DIR}/../server-browser/src")
find_path(REBOOT_PFR_INCLUDE boost/pfr.hpp HINTS ${Boost_INCLUDE_DIRS} REQUIRED)
install(DIRECTORY include/ "${PROJECT_SOURCE_DIR}/core/api/include/" "${PROJECT_SOURCE_DIR}/core/base/include/"
        DESTINATION include COMPONENT sdk)
install(FILES "${PROJECT_SOURCE_DIR}/core/client/include/reboot/client.h"
              "${PROJECT_SOURCE_DIR}/core/client/include/reboot/client_methods.h"
        DESTINATION include/reboot COMPONENT sdk)
install(FILES "${sb_source}/core/features.hpp" "${sb_source}/core/types.hpp" DESTINATION include/core COMPONENT sdk)
install(FILES "${sb_source}/wire/buffer.hpp" "${sb_source}/wire/codec.hpp" DESTINATION include/wire COMPONENT sdk)
install(FILES "${REBOOT_PFR_INCLUDE}/boost/pfr.hpp" DESTINATION include/boost COMPONENT sdk)
install(DIRECTORY "${REBOOT_PFR_INCLUDE}/boost/pfr" DESTINATION include/boost COMPONENT sdk)
install(TARGETS reboot_client
        RUNTIME DESTINATION bin COMPONENT sdk LIBRARY DESTINATION lib COMPONENT sdk ARCHIVE DESTINATION lib COMPONENT sdk)

if(REBOOT_BUILD_TESTS)
  # Through the real reboot_client against the engine rig on the OS's own endpoint.
  set(sdk_test_sources tests/context_tests.cpp tests/error_tests.cpp tests/sdk_rig.hpp)
  add_executable(reboot_sdk_tests ${sdk_test_sources})
  target_include_directories(reboot_sdk_tests PRIVATE "${PROJECT_SOURCE_DIR}/core/engine/tests")
  # reboot_ipc ahead of the OS ipc package, which defines the endpoint_name it calls.
  target_link_libraries(reboot_sdk_tests PRIVATE
    reboot_sdk reboot_engine reboot::logging reboot::testing reboot::ipc reboot_os_${REBOOT_OS}_ipc Boost::asio
    Catch2::Catch2WithMain reboot_warnings)
  _reboot_record_target(reboot_sdk_tests test DEPS reboot::sdk reboot::api reboot::ipc reboot::engine reboot::logging reboot::testing
    SOURCES ${sdk_test_sources})
  if(MSVC)
    # The rig and every typed method instantiate more sections than a default COFF object holds.
    target_compile_options(reboot_sdk_tests PRIVATE /bigobj)
  endif()
  if(WIN32)
    # Ahead of test discovery, which runs the executable.
    add_custom_command(TARGET reboot_sdk_tests POST_BUILD
      COMMAND "${CMAKE_COMMAND}" -E copy_if_different "$<TARGET_FILE:reboot_client>" "$<TARGET_FILE_DIR:reboot_sdk_tests>"
      VERBATIM)
  endif()
  if(NOT COMMAND catch_discover_tests)
    include(Catch)
  endif()
  catch_discover_tests(reboot_sdk_tests)
endif()
