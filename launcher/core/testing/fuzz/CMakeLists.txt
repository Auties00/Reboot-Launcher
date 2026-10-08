# With REBOOT_LIBFUZZER the targets link libFuzzer (Clang -fsanitize=fuzzer, MSVC /fsanitize=fuzzer).
# Otherwise each gets replay_main.cpp, and CI replays its committed corpus/<name>/ as a test.
option(REBOOT_LIBFUZZER "Build the fuzz targets against libFuzzer" OFF)
if(REBOOT_LIBFUZZER AND NOT MSVC AND NOT CMAKE_CXX_COMPILER_ID MATCHES "Clang")
  message(FATAL_ERROR "REBOOT_LIBFUZZER needs Clang or MSVC; GCC has no -fsanitize=fuzzer")
endif()
set(REBOOT_FUZZ_DIR "${CMAKE_CURRENT_SOURCE_DIR}")
# frame_fuzz.hpp is the testing package's private header.
set(REBOOT_TESTING_SRC_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../src")

# reboot_add_fuzz_target(<name> CORPUS <dir> SOURCES ... [DEPS ...])
function(reboot_add_fuzz_target name)
  cmake_parse_arguments(PARSE_ARGV 1 arg "" "CORPUS" "SOURCES;DEPS")
  set(corpus "${REBOOT_FUZZ_DIR}/corpus/${arg_CORPUS}")
  if(NOT IS_DIRECTORY "${corpus}")
    message(FATAL_ERROR "${name}: commit at least one seed under ${corpus}")
  endif()
  reboot_add_executable(${name} SOURCES ${arg_SOURCES} DEPS reboot::testing ${arg_DEPS})
  target_include_directories(${name} PRIVATE "${REBOOT_TESTING_SRC_DIR}")
  if(REBOOT_LIBFUZZER)
    if(MSVC)
      target_compile_options(${name} PRIVATE /fsanitize=fuzzer)
    else()
      target_compile_options(${name} PRIVATE -fsanitize=fuzzer)
      target_link_options(${name} PRIVATE -fsanitize=fuzzer)
    endif()
  else()
    target_sources(${name} PRIVATE "${REBOOT_FUZZ_DIR}/replay_main.cpp")
    add_test(NAME ${name} COMMAND ${name} "${corpus}")
  endif()
endfunction()

reboot_add_fuzz_target(reboot-fuzz-ipc-frames CORPUS ipc_frames SOURCES ipc_frames_fuzz.cpp)
reboot_add_fuzz_target(reboot-fuzz-api-payloads CORPUS api_payloads SOURCES api_payloads_fuzz.cpp DEPS reboot::api)
reboot_add_fuzz_target(reboot-fuzz-child-frames CORPUS child_frames SOURCES child_frames_fuzz.cpp)
reboot_add_fuzz_target(reboot-fuzz-game-control-frames CORPUS game_control_frames SOURCES game_control_frames_fuzz.cpp)
reboot_add_fuzz_target(reboot-fuzz-addresses CORPUS addresses SOURCES addresses_fuzz.cpp)

# Targets over other packages' parsers live in their own directories, so the DAG check sees each
# one's dependency apart; each is skipped until its package exists.
add_subdirectory(line_reader)
add_subdirectory(pe_version_reader)
add_subdirectory(deep_link)
