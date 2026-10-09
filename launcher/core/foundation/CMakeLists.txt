add_library(reboot_foundation STATIC
  src/cancel.cpp
  src/diag.cpp
  src/events.cpp
  src/executor.cpp
  src/log.cpp
  src/messages.cpp
  src/native_path.cpp
  src/net_types.cpp
  src/operation.cpp
  src/paths.cpp
  src/random.cpp
  src/random_os.cpp
  src/sha256.cpp
  src/text.cpp
  src/types.cpp
  src/user_request.cpp
  src/version.cpp)
add_library(reboot::foundation ALIAS reboot_foundation)
target_include_directories(reboot_foundation
  PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include"
  # paths.cpp calls IPlatformPaths; ports is part of the foundation but links foundation itself.
  PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src" "${CMAKE_CURRENT_SOURCE_DIR}/../ports/include")
target_link_libraries(reboot_foundation
  PUBLIC sb_contract Boost::headers Boost::asio Boost::container Boost::unordered Threads::Threads
  PRIVATE reboot_warnings $<$<PLATFORM_ID:Windows>:bcrypt>)
_reboot_record_target(reboot_foundation foundation)

if(REBOOT_BUILD_TESTS)
  file(GLOB foundation_tests CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/tests/*.cpp")
  if(foundation_tests)
    add_executable(reboot_foundation_tests ${foundation_tests})
    target_include_directories(reboot_foundation_tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
    target_link_libraries(reboot_foundation_tests PRIVATE reboot_foundation reboot_ports reboot_contracts
                                                          $<TARGET_NAME_IF_EXISTS:reboot_testing>
                                                          Catch2::Catch2WithMain reboot_warnings)
    include(Catch)
    catch_discover_tests(reboot_foundation_tests)
  endif()
endif()
