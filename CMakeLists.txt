cmake_minimum_required(VERSION 3.20)
project(moba_sim CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON CACHE INTERNAL "")

if(CMAKE_EXPORT_COMPILE_COMMANDS)
  set(CMAKE_CXX_STANDARD_INCLUDE_DIRECTORIES
      ${CMAKE_CXX_IMPLICIT_INCLUDE_DIRECTORIES})
endif()

add_subdirectory(src)

option(MOBA_SIM_BUILD_TESTS "Build C++ tests" ON)
if(MOBA_SIM_BUILD_TESTS)
  enable_testing()
  add_subdirectory(tests)
endif()

option(MOBA_SIM_BUILD_PYTHON "Build Python bindings" OFF)
if(MOBA_SIM_BUILD_PYTHON)
  add_subdirectory(python)
endif()
