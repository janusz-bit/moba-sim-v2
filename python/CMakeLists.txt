find_package(Python COMPONENTS Interpreter Development.Module REQUIRED)

# nanobind ships its CMake config inside the Python site-packages tree.
# Resolve it via Python so we don't depend on CMAKE_PREFIX_PATH hacks.
execute_process(
  COMMAND ${Python_EXECUTABLE} -c
          "import nanobind, os; print(os.path.join(os.path.dirname(os.path.dirname(nanobind.__file__)), 'nanobind', 'cmake'))"
  OUTPUT_VARIABLE NANOBIND_CMAKE_DIR
  OUTPUT_STRIP_TRAILING_WHITESPACE
  RESULT_VARIABLE NANOBIND_RESULT
)
if(NOT NANOBIND_RESULT EQUAL 0)
  message(FATAL_ERROR "Could not locate nanobind's cmake directory. "
                       "Is nanobind installed?")
endif()
list(APPEND CMAKE_PREFIX_PATH "${NANOBIND_CMAKE_DIR}")
find_package(nanobind CONFIG REQUIRED)

nanobind_add_module(
    moba_ext
    NB_STATIC STABLE_ABI
    ${CMAKE_CURRENT_SOURCE_DIR}/moba_sim_ext.cpp
)
target_link_libraries(moba_ext PRIVATE moba_sim)

# Install the extension next to the Python package (python/moba/).
install(TARGETS moba_ext LIBRARY DESTINATION moba)

# Also copy the extension into the source tree so the package is directly
# importable during development (PYTHONPATH=python).
add_custom_command(
  TARGET moba_ext POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_FILE:moba_ext>
          ${CMAKE_CURRENT_SOURCE_DIR}/moba/$<TARGET_FILE_NAME:moba_ext>
  COMMENT "Copying moba_ext to python/moba/")