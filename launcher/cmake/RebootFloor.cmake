include_guard(GLOBAL)

# The CI floor job builds every target with MSVC, Apple Clang/libc++ and GCC 15 with this ON;
# floor_probe additionally fails on facilities one of them lacks.
option(REBOOT_FLOOR_CHECK "Fail the build on language or library facilities outside the toolchain floor" OFF)

if(REBOOT_FLOOR_CHECK)
  find_package(Python3 REQUIRED COMPONENTS Interpreter)
  add_custom_target(floor_probe ALL
    COMMAND Python3::Interpreter "${CMAKE_CURRENT_LIST_DIR}/floor_check.py"
            "${CMAKE_SOURCE_DIR}/core" "${CMAKE_SOURCE_DIR}/core-windows"
            "${CMAKE_SOURCE_DIR}/core-macos" "${CMAKE_SOURCE_DIR}/core-linux"
    COMMENT "Checking sources against the toolchain floor"
    VERBATIM)
endif()
