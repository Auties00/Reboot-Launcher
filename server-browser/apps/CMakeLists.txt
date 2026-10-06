function(sb_app name)
  add_executable(${name} ${ARGN})
  target_link_libraries(${name} PRIVATE sb_net CLI11::CLI11 sb_warnings)
  if(SB_USE_MIMALLOC)
    # Whole-archive so mimalloc's malloc/free replace libc's for every allocation in the process.
    target_link_libraries(${name} PRIVATE $<LINK_LIBRARY:WHOLE_ARCHIVE,mimalloc-static>)
  endif()
  install(TARGETS ${name} RUNTIME DESTINATION bin)
endfunction()

sb_app(sb-edge sb-edge/main.cpp)
sb_app(sb-cli sb-cli/main.cpp)
sb_app(sb-loadgen sb-loadgen/main.cpp)
