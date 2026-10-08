# vcpkg has no velopack port and the library is Rust, so this installs the release's static libraries.
vcpkg_download_distfile(archive
    URLS "https://github.com/velopack/velopack/releases/download/${VERSION}/velopack_libc_${VERSION}.zip"
    FILENAME "velopack_libc_${VERSION}.zip"
    SHA512 ad0d9ded09182046cdd113ef2283b07adf633e5a1655f22a1595bc784821fa6a9cd2a22c218fb0e3c54e923d8936dfb08575f8c1c3a08b607ae39a16ec632536)
vcpkg_download_distfile(license
    URLS "https://raw.githubusercontent.com/velopack/velopack/${VERSION}/LICENSE"
    FILENAME "velopack-${VERSION}-LICENSE"
    SHA512 dc444a747921f1803473a78ad6a1071ee51249a0ca66d18c515bd33126d15a197492e6606b690505ed64a384085e6ab41bbad5dfe79a330a124cf6a453456ade)
vcpkg_extract_source_archive(source_path ARCHIVE "${archive}" NO_REMOVE_ONE_LEVEL)

if(VCPKG_TARGET_IS_WINDOWS)
    set(release_lib "velopack_libc_win_x64_msvc.lib")
elseif(VCPKG_TARGET_ARCHITECTURE STREQUAL "arm64")
    set(release_lib "velopack_libc_osx_arm64_gnu.a")
else()
    set(release_lib "velopack_libc_osx_x64_gnu.a")
endif()
set(lib_name "${VCPKG_TARGET_STATIC_LIBRARY_PREFIX}velopack_libc${VCPKG_TARGET_STATIC_LIBRARY_SUFFIX}")

file(INSTALL "${source_path}/include/" DESTINATION "${CURRENT_PACKAGES_DIR}/include")
foreach(dir IN ITEMS lib debug/lib)
    file(INSTALL "${source_path}/lib-static/${release_lib}" DESTINATION "${CURRENT_PACKAGES_DIR}/${dir}" RENAME "${lib_name}")
endforeach()

file(WRITE "${CURRENT_PACKAGES_DIR}/share/${PORT}/velopack-config.cmake" "\
get_filename_component(_velopack_prefix \"\${CMAKE_CURRENT_LIST_DIR}/../..\" ABSOLUTE)
if(NOT TARGET velopack::velopack)
  add_library(velopack::velopack STATIC IMPORTED)
  set_target_properties(velopack::velopack PROPERTIES
    IMPORTED_LOCATION \"\${_velopack_prefix}/lib/${lib_name}\"
    INTERFACE_INCLUDE_DIRECTORIES \"\${_velopack_prefix}/include\")
endif()
unset(_velopack_prefix)
")
vcpkg_install_copyright(FILE_LIST "${license}")

# The release library (static CRT) also serves Debug, so the debug CRT check cannot pass.
set(VCPKG_POLICY_SKIP_CRT_LINKAGE_CHECK enabled)
