cmake_minimum_required(VERSION 3.20)
project(qingying LANGUAGES CXX)

include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/CompilerOptions.cmake")
include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/OutputDirectories.cmake")
include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/JsonDependency.cmake")

add_subdirectory(src)

option(QINGYING_BUILD_TESTS "Build GoogleTest unit tests" ON)

if(QINGYING_BUILD_TESTS)
  get_filename_component(QINGYING_VCPKG_ROOT
    "${CMAKE_SOURCE_DIR}/../thirdparty_install/vcpkg"
    ABSOLUTE)
  set(QINGYING_VCPKG_ROOT "${QINGYING_VCPKG_ROOT}"
      CACHE PATH "vcpkg root containing installed GTest" FORCE)
  list(APPEND CMAKE_PREFIX_PATH
       "${QINGYING_VCPKG_ROOT}/installed/x64-windows")
  find_package(GTest CONFIG REQUIRED)
  enable_testing()
  include(CTest)
  add_subdirectory(tests)
endif()
