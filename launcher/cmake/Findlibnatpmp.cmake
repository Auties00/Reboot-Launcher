# vcpkg's libnatpmp installs no CMake package, only the library, its headers and a .pc file.
find_path(libnatpmp_INCLUDE_DIR natpmp.h)
find_library(libnatpmp_LIBRARY NAMES natpmp)

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(libnatpmp REQUIRED_VARS libnatpmp_LIBRARY libnatpmp_INCLUDE_DIR)

if(libnatpmp_FOUND AND NOT TARGET libnatpmp::natpmp)
  add_library(libnatpmp::natpmp UNKNOWN IMPORTED)
  set_target_properties(libnatpmp::natpmp PROPERTIES
    IMPORTED_LOCATION "${libnatpmp_LIBRARY}"
    INTERFACE_INCLUDE_DIRECTORIES "${libnatpmp_INCLUDE_DIR}")
  if(WIN32)
    set_property(TARGET libnatpmp::natpmp PROPERTY INTERFACE_LINK_LIBRARIES ws2_32 iphlpapi)
  endif()
endif()
