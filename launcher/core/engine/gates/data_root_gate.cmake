# CI gate: run from inside a Velopack package (Windows) or an app bundle (macOS), neither the default
# roots nor a REBOOT_LAUNCHER_HOME pointing into the package put the engine's data there.
# cmake -DPROBE=<reboot-data-root-probe> -DENGINE=<reboot-engine> -DWORK=<scratch dir> -P data_root_gate.cmake
foreach(variable PROBE ENGINE WORK)
  if(NOT ${variable})
    message(FATAL_ERROR "${variable} is not set")
  endif()
endforeach()

file(REMOVE_RECURSE "${WORK}")
if(CMAKE_HOST_WIN32)
  set(package "${WORK}/Reboot Launcher")
  set(bin "${package}/current")
  file(MAKE_DIRECTORY "${bin}")
  file(TOUCH "${package}/Update.exe" "${bin}/sq.version")
  # Lexically inside, though spelled differently; Windows paths ignore case.
  set(homes "${bin}/data" "${package}" "${bin}/../current/data" "${WORK}/REBOOT LAUNCHER/Current/Data")
elseif(CMAKE_HOST_APPLE)
  set(package "${WORK}/Reboot Launcher.app")
  set(bin "${package}/Contents/MacOS")
  file(MAKE_DIRECTORY "${bin}")
  set(homes "${package}/Contents/Resources/data" "${package}" "${bin}/../Resources/../data")
else()
  message(FATAL_ERROR "only Windows and macOS installs live in a Velopack package")
endif()

# The executables' shared libraries, such as msquic.dll, travel with them.
get_filename_component(engine_dir "${ENGINE}" DIRECTORY)
file(GLOB libraries "${engine_dir}/*.dll" "${engine_dir}/*.dylib")
file(COPY "${PROBE}" "${ENGINE}" ${libraries} DESTINATION "${bin}")
get_filename_component(probe_name "${PROBE}" NAME)
get_filename_component(engine_name "${ENGINE}" NAME)

unset(ENV{REBOOT_LAUNCHER_HOME})
execute_process(COMMAND "${bin}/${probe_name}" RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE error
                TIMEOUT 60)
message(STATUS "${output}${error}")
if(NOT output MATCHES "package=[^\n]+")
  message(FATAL_ERROR "the probe did not recognise ${package} as its package")
endif()
if(NOT result EQUAL 0)
  message(FATAL_ERROR "a default root is inside the package (probe exit ${result})")
endif()

# The engine refuses such a root at startup step 1, before it takes a lock or writes anything.
foreach(home IN LISTS homes)
  set(ENV{REBOOT_LAUNCHER_HOME} "${home}")
  execute_process(COMMAND "${bin}/${engine_name}" run RESULT_VARIABLE result OUTPUT_VARIABLE output
                  ERROR_VARIABLE error TIMEOUT 60)
  if(result EQUAL 0 OR NOT error MATCHES "storage\\.root_inside_package")
    message(FATAL_ERROR "the engine accepted the data root ${home} inside ${package}: exit ${result}, ${output}${error}")
  endif()
  message(STATUS "refused ${home}")
endforeach()
unset(ENV{REBOOT_LAUNCHER_HOME})
file(REMOVE_RECURSE "${WORK}")
