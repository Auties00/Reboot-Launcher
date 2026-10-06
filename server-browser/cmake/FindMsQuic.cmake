# Locates MsQuic >= 2.5 (app-owned execution). vcpkg still ships 2.4, so the Docker builder installs 2.6 into /opt/msquic.
find_path(MsQuic_INCLUDE_DIR msquic.h HINTS ${MsQuic_ROOT} ENV MsQuic_ROOT /opt/msquic PATH_SUFFIXES include)
find_library(MsQuic_LIBRARY NAMES msquic HINTS ${MsQuic_ROOT} ENV MsQuic_ROOT /opt/msquic PATH_SUFFIXES lib lib64)

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(MsQuic REQUIRED_VARS MsQuic_LIBRARY MsQuic_INCLUDE_DIR)

if(MsQuic_FOUND AND NOT TARGET MsQuic::MsQuic)
  add_library(MsQuic::MsQuic UNKNOWN IMPORTED)
  set_target_properties(MsQuic::MsQuic PROPERTIES
    IMPORTED_LOCATION "${MsQuic_LIBRARY}"
    INTERFACE_INCLUDE_DIRECTORIES "${MsQuic_INCLUDE_DIR}"
    INTERFACE_COMPILE_DEFINITIONS "QUIC_API_ENABLE_PREVIEW_FEATURES=1")
endif()
