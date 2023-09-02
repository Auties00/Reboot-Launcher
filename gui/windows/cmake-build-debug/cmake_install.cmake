# Install script for directory: C:/Users/alaut/AndroidStudioProjects/reboot_launcher/windows

# Set the install prefix
if(NOT DEFINED CMAKE_INSTALL_PREFIX)
  set(CMAKE_INSTALL_PREFIX "$<TARGET_FILE_DIR:reboot_launcher>")
endif()
string(REGEX REPLACE "/$" "" CMAKE_INSTALL_PREFIX "${CMAKE_INSTALL_PREFIX}")

# Set the install configuration name.
if(NOT DEFINED CMAKE_INSTALL_CONFIG_NAME)
  if(BUILD_TYPE)
    string(REGEX REPLACE "^[^A-Za-z0-9_]+" ""
           CMAKE_INSTALL_CONFIG_NAME "${BUILD_TYPE}")
  else()
    set(CMAKE_INSTALL_CONFIG_NAME "Debug")
  endif()
  message(STATUS "Install configuration: \"${CMAKE_INSTALL_CONFIG_NAME}\"")
endif()

# Set the component getting installed.
if(NOT CMAKE_INSTALL_COMPONENT)
  if(COMPONENT)
    message(STATUS "Install component: \"${COMPONENT}\"")
    set(CMAKE_INSTALL_COMPONENT "${COMPONENT}")
  else()
    set(CMAKE_INSTALL_COMPONENT)
  endif()
endif()

# Is this installation the result of a crosscompile?
if(NOT DEFINED CMAKE_CROSSCOMPILING)
  set(CMAKE_CROSSCOMPILING "FALSE")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("C:/Users/alaut/AndroidStudioProjects/reboot_launcher/windows/cmake-build-debug/flutter/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("C:/Users/alaut/AndroidStudioProjects/reboot_launcher/windows/cmake-build-debug/runner/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("C:/Users/alaut/AndroidStudioProjects/reboot_launcher/windows/cmake-build-debug/plugins/app_links/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("C:/Users/alaut/AndroidStudioProjects/reboot_launcher/windows/cmake-build-debug/plugins/bitsdojo_window_windows/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("C:/Users/alaut/AndroidStudioProjects/reboot_launcher/windows/cmake-build-debug/plugins/flutter_acrylic/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("C:/Users/alaut/AndroidStudioProjects/reboot_launcher/windows/cmake-build-debug/plugins/screen_retriever/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("C:/Users/alaut/AndroidStudioProjects/reboot_launcher/windows/cmake-build-debug/plugins/system_theme/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("C:/Users/alaut/AndroidStudioProjects/reboot_launcher/windows/cmake-build-debug/plugins/url_launcher_windows/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("C:/Users/alaut/AndroidStudioProjects/reboot_launcher/windows/cmake-build-debug/plugins/window_manager/cmake_install.cmake")
endif()

if("x${CMAKE_INSTALL_COMPONENT}x" STREQUAL "xRuntimex" OR NOT CMAKE_INSTALL_COMPONENT)
  list(APPEND CMAKE_ABSOLUTE_DESTINATION_FILES
   "C:/Users/alaut/AndroidStudioProjects/reboot_launcher/windows/cmake-build-debug/runner/reboot_launcher.exe")
  if(CMAKE_WARN_ON_ABSOLUTE_INSTALL_DESTINATION)
    message(WARNING "ABSOLUTE path INSTALL DESTINATION : ${CMAKE_ABSOLUTE_DESTINATION_FILES}")
  endif()
  if(CMAKE_ERROR_ON_ABSOLUTE_INSTALL_DESTINATION)
    message(FATAL_ERROR "ABSOLUTE path INSTALL DESTINATION forbidden (by caller): ${CMAKE_ABSOLUTE_DESTINATION_FILES}")
  endif()
file(INSTALL DESTINATION "C:/Users/alaut/AndroidStudioProjects/reboot_launcher/windows/cmake-build-debug/runner" TYPE EXECUTABLE FILES "C:/Users/alaut/AndroidStudioProjects/reboot_launcher/windows/cmake-build-debug/runner/reboot_launcher.exe")
endif()

if("x${CMAKE_INSTALL_COMPONENT}x" STREQUAL "xRuntimex" OR NOT CMAKE_INSTALL_COMPONENT)
  list(APPEND CMAKE_ABSOLUTE_DESTINATION_FILES
   "C:/Users/alaut/AndroidStudioProjects/reboot_launcher/windows/cmake-build-debug/runner/data/icudtl.dat")
  if(CMAKE_WARN_ON_ABSOLUTE_INSTALL_DESTINATION)
    message(WARNING "ABSOLUTE path INSTALL DESTINATION : ${CMAKE_ABSOLUTE_DESTINATION_FILES}")
  endif()
  if(CMAKE_ERROR_ON_ABSOLUTE_INSTALL_DESTINATION)
    message(FATAL_ERROR "ABSOLUTE path INSTALL DESTINATION forbidden (by caller): ${CMAKE_ABSOLUTE_DESTINATION_FILES}")
  endif()
file(INSTALL DESTINATION "C:/Users/alaut/AndroidStudioProjects/reboot_launcher/windows/cmake-build-debug/runner/data" TYPE FILE FILES "C:/Users/alaut/AndroidStudioProjects/reboot_launcher/windows/flutter/ephemeral/icudtl.dat")
endif()

if("x${CMAKE_INSTALL_COMPONENT}x" STREQUAL "xRuntimex" OR NOT CMAKE_INSTALL_COMPONENT)
  list(APPEND CMAKE_ABSOLUTE_DESTINATION_FILES
   "C:/Users/alaut/AndroidStudioProjects/reboot_launcher/windows/cmake-build-debug/runner/flutter_windows.dll")
  if(CMAKE_WARN_ON_ABSOLUTE_INSTALL_DESTINATION)
    message(WARNING "ABSOLUTE path INSTALL DESTINATION : ${CMAKE_ABSOLUTE_DESTINATION_FILES}")
  endif()
  if(CMAKE_ERROR_ON_ABSOLUTE_INSTALL_DESTINATION)
    message(FATAL_ERROR "ABSOLUTE path INSTALL DESTINATION forbidden (by caller): ${CMAKE_ABSOLUTE_DESTINATION_FILES}")
  endif()
file(INSTALL DESTINATION "C:/Users/alaut/AndroidStudioProjects/reboot_launcher/windows/cmake-build-debug/runner" TYPE FILE FILES "C:/Users/alaut/AndroidStudioProjects/reboot_launcher/windows/flutter/ephemeral/flutter_windows.dll")
endif()

if("x${CMAKE_INSTALL_COMPONENT}x" STREQUAL "xRuntimex" OR NOT CMAKE_INSTALL_COMPONENT)
  list(APPEND CMAKE_ABSOLUTE_DESTINATION_FILES
   "C:/Users/alaut/AndroidStudioProjects/reboot_launcher/windows/cmake-build-debug/runner/app_links_plugin.dll;C:/Users/alaut/AndroidStudioProjects/reboot_launcher/windows/cmake-build-debug/runner/bitsdojo_window_windows_plugin.lib;C:/Users/alaut/AndroidStudioProjects/reboot_launcher/windows/cmake-build-debug/runner/flutter_acrylic_plugin.dll;C:/Users/alaut/AndroidStudioProjects/reboot_launcher/windows/cmake-build-debug/runner/screen_retriever_plugin.dll;C:/Users/alaut/AndroidStudioProjects/reboot_launcher/windows/cmake-build-debug/runner/system_theme_plugin.dll;C:/Users/alaut/AndroidStudioProjects/reboot_launcher/windows/cmake-build-debug/runner/url_launcher_windows_plugin.dll;C:/Users/alaut/AndroidStudioProjects/reboot_launcher/windows/cmake-build-debug/runner/window_manager_plugin.dll")
  if(CMAKE_WARN_ON_ABSOLUTE_INSTALL_DESTINATION)
    message(WARNING "ABSOLUTE path INSTALL DESTINATION : ${CMAKE_ABSOLUTE_DESTINATION_FILES}")
  endif()
  if(CMAKE_ERROR_ON_ABSOLUTE_INSTALL_DESTINATION)
    message(FATAL_ERROR "ABSOLUTE path INSTALL DESTINATION forbidden (by caller): ${CMAKE_ABSOLUTE_DESTINATION_FILES}")
  endif()
file(INSTALL DESTINATION "C:/Users/alaut/AndroidStudioProjects/reboot_launcher/windows/cmake-build-debug/runner" TYPE FILE FILES
    "C:/Users/alaut/AndroidStudioProjects/reboot_launcher/windows/cmake-build-debug/plugins/app_links/app_links_plugin.dll"
    "C:/Users/alaut/AndroidStudioProjects/reboot_launcher/windows/cmake-build-debug/plugins/bitsdojo_window_windows/bitsdojo_window_windows_plugin.lib"
    "C:/Users/alaut/AndroidStudioProjects/reboot_launcher/windows/cmake-build-debug/plugins/flutter_acrylic/flutter_acrylic_plugin.dll"
    "C:/Users/alaut/AndroidStudioProjects/reboot_launcher/windows/cmake-build-debug/plugins/screen_retriever/screen_retriever_plugin.dll"
    "C:/Users/alaut/AndroidStudioProjects/reboot_launcher/windows/cmake-build-debug/plugins/system_theme/system_theme_plugin.dll"
    "C:/Users/alaut/AndroidStudioProjects/reboot_launcher/windows/cmake-build-debug/plugins/url_launcher_windows/url_launcher_windows_plugin.dll"
    "C:/Users/alaut/AndroidStudioProjects/reboot_launcher/windows/cmake-build-debug/plugins/window_manager/window_manager_plugin.dll"
    )
endif()

if("x${CMAKE_INSTALL_COMPONENT}x" STREQUAL "xRuntimex" OR NOT CMAKE_INSTALL_COMPONENT)
  
  file(REMOVE_RECURSE "C:/Users/alaut/AndroidStudioProjects/reboot_launcher/windows/cmake-build-debug/runner/data/flutter_assets")
  
endif()

if("x${CMAKE_INSTALL_COMPONENT}x" STREQUAL "xRuntimex" OR NOT CMAKE_INSTALL_COMPONENT)
  list(APPEND CMAKE_ABSOLUTE_DESTINATION_FILES
   "C:/Users/alaut/AndroidStudioProjects/reboot_launcher/windows/cmake-build-debug/runner/data/flutter_assets")
  if(CMAKE_WARN_ON_ABSOLUTE_INSTALL_DESTINATION)
    message(WARNING "ABSOLUTE path INSTALL DESTINATION : ${CMAKE_ABSOLUTE_DESTINATION_FILES}")
  endif()
  if(CMAKE_ERROR_ON_ABSOLUTE_INSTALL_DESTINATION)
    message(FATAL_ERROR "ABSOLUTE path INSTALL DESTINATION forbidden (by caller): ${CMAKE_ABSOLUTE_DESTINATION_FILES}")
  endif()
file(INSTALL DESTINATION "C:/Users/alaut/AndroidStudioProjects/reboot_launcher/windows/cmake-build-debug/runner/data" TYPE DIRECTORY FILES "C:/Users/alaut/AndroidStudioProjects/reboot_launcher/build//flutter_assets")
endif()

if("x${CMAKE_INSTALL_COMPONENT}x" STREQUAL "xRuntimex" OR NOT CMAKE_INSTALL_COMPONENT)
  if("${CMAKE_INSTALL_CONFIG_NAME}" MATCHES "^([Pp][Rr][Oo][Ff][Ii][Ll][Ee]|[Rr][Ee][Ll][Ee][Aa][Ss][Ee])$")
    list(APPEND CMAKE_ABSOLUTE_DESTINATION_FILES
     "C:/Users/alaut/AndroidStudioProjects/reboot_launcher/windows/cmake-build-debug/runner/data/app.so")
    if(CMAKE_WARN_ON_ABSOLUTE_INSTALL_DESTINATION)
        message(WARNING "ABSOLUTE path INSTALL DESTINATION : ${CMAKE_ABSOLUTE_DESTINATION_FILES}")
    endif()
    if(CMAKE_ERROR_ON_ABSOLUTE_INSTALL_DESTINATION)
        message(FATAL_ERROR "ABSOLUTE path INSTALL DESTINATION forbidden (by caller): ${CMAKE_ABSOLUTE_DESTINATION_FILES}")
    endif()
file(INSTALL DESTINATION "C:/Users/alaut/AndroidStudioProjects/reboot_launcher/windows/cmake-build-debug/runner/data" TYPE FILE FILES "C:/Users/alaut/AndroidStudioProjects/reboot_launcher/build/windows/app.so")
  endif("${CMAKE_INSTALL_CONFIG_NAME}" MATCHES "^([Pp][Rr][Oo][Ff][Ii][Ll][Ee]|[Rr][Ee][Ll][Ee][Aa][Ss][Ee])$")
endif()

if(CMAKE_INSTALL_COMPONENT)
  set(CMAKE_INSTALL_MANIFEST "install_manifest_${CMAKE_INSTALL_COMPONENT}.txt")
else()
  set(CMAKE_INSTALL_MANIFEST "install_manifest.txt")
endif()

string(REPLACE ";" "\n" CMAKE_INSTALL_MANIFEST_CONTENT
       "${CMAKE_INSTALL_MANIFEST_FILES}")
file(WRITE "C:/Users/alaut/AndroidStudioProjects/reboot_launcher/windows/cmake-build-debug/${CMAKE_INSTALL_MANIFEST}"
     "${CMAKE_INSTALL_MANIFEST_CONTENT}")
