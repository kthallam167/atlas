cmake_minimum_required(VERSION 3.16)
project(atlas LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()
set(CMAKE_CXX_FLAGS_RELEASE "-O3")

# Core search library.
add_library(atlas_core
  cpp/src/tokenizer.cpp
  cpp/src/index_builder.cpp
  cpp/src/index.cpp
  cpp/src/query_parser.cpp
  cpp/src/searcher.cpp)
target_include_directories(atlas_core PUBLIC cpp/include)

# Command-line tool.
add_executable(atlas cpp/tools/atlas_cli.cpp)
target_link_libraries(atlas PRIVATE atlas_core)

# Unit tests.
enable_testing()
add_executable(atlas_tests cpp/tests/test_atlas.cpp)
target_link_libraries(atlas_tests PRIVATE atlas_core)
add_test(NAME atlas_tests COMMAND atlas_tests)

# Python bindings (optional; enabled by default when pybind11 is available).
option(ATLAS_BUILD_PYTHON "Build the pybind11 module" ON)
if(ATLAS_BUILD_PYTHON)
  find_package(pybind11 CONFIG QUIET)
  if(pybind11_FOUND)
    pybind11_add_module(atlas_native cpp/bindings/pyatlas.cpp)
    target_link_libraries(atlas_native PRIVATE atlas_core)
  else()
    message(WARNING "pybind11 not found; skipping Python module")
  endif()
endif()
