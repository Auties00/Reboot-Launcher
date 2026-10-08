include_guard(GLOBAL)

find_package(Python3 COMPONENTS Interpreter)
set(_REBOOT_DAG_SCRIPT "${CMAKE_CURRENT_LIST_DIR}/dag_check.py")

# Runs once the whole tree is configured, so every package has recorded its DEPS.
function(_reboot_finish_dag_check)
  get_property(targets GLOBAL PROPERTY REBOOT_TARGETS)
  set(manifest "")
  foreach(target IN LISTS targets)
    get_property(kind GLOBAL PROPERTY REBOOT_KIND_${target})
    get_property(dir GLOBAL PROPERTY REBOOT_DIR_${target})
    get_property(deps GLOBAL PROPERTY REBOOT_DEPS_${target})
    get_property(sources GLOBAL PROPERTY REBOOT_SOURCES_${target})
    string(APPEND manifest "${target}|${kind}|${dir}|${deps}|${sources}\n")
  endforeach()
  set(manifest_file "${CMAKE_BINARY_DIR}/reboot_dag.txt")
  file(WRITE "${manifest_file}" "${manifest}")

  if(NOT Python3_Interpreter_FOUND)
    message(WARNING "Python 3 not found: dag_check is unavailable")
    return()
  endif()
  set(client_args "")
  if(TARGET reboot_client)
    set(client_args --client "$<TARGET_FILE:reboot_client>")
  endif()
  add_custom_target(dag_check
    COMMAND Python3::Interpreter "${_REBOOT_DAG_SCRIPT}"
            --manifest "${manifest_file}" --root "${CMAKE_SOURCE_DIR}" ${client_args}
    COMMENT "Checking the package dependency DAG"
    VERBATIM)
endfunction()

cmake_language(DEFER CALL _reboot_finish_dag_check)
