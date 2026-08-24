cmake_minimum_required(VERSION 3.20)
project(qingying LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(MSVC)
  add_compile_options(/utf-8 /W4)
endif()

# 统一导出目录，便于核对单文件体积
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)

add_library(qingying_action STATIC
  src/action/action_dispatcher.cpp
)
target_include_directories(qingying_action
  PUBLIC ${CMAKE_SOURCE_DIR}/include
)

add_library(qingying_capture STATIC
  src/capture/capture_engine.cpp
)
target_include_directories(qingying_capture
  PUBLIC ${CMAKE_SOURCE_DIR}/include
)
target_link_libraries(qingying_capture PUBLIC qingying_action)
if(WIN32)
  target_link_libraries(qingying_capture PRIVATE d3d11 dxgi)
endif()

add_library(qingying_export STATIC
  src/export/export_service.cpp
)
target_include_directories(qingying_export
  PUBLIC ${CMAKE_SOURCE_DIR}/include
)
target_link_libraries(qingying_export PUBLIC qingying_action)

add_library(qingying_overlay STATIC
  src/overlay/selection_overlay.cpp
)
target_include_directories(qingying_overlay
  PUBLIC ${CMAKE_SOURCE_DIR}/include
)
target_link_libraries(qingying_overlay PUBLIC qingying_action qingying_capture)

add_library(qingying_annotate STATIC
  src/annotate/annotation_engine.cpp
)
target_include_directories(qingying_annotate
  PUBLIC ${CMAKE_SOURCE_DIR}/include
)
target_link_libraries(qingying_annotate PUBLIC qingying_action)

add_library(qingying_pin STATIC
  src/pin/pin_manager.cpp
)
target_include_directories(qingying_pin
  PUBLIC ${CMAKE_SOURCE_DIR}/include
)
target_link_libraries(qingying_pin PUBLIC qingying_action)

add_library(qingying_longshot STATIC
  src/longshot/longshot_engine.cpp
)
target_include_directories(qingying_longshot
  PUBLIC ${CMAKE_SOURCE_DIR}/include
)
target_link_libraries(qingying_longshot PUBLIC qingying_action qingying_capture)

add_library(qingying_command STATIC
  src/command/command_parser.cpp
)
target_include_directories(qingying_command
  PUBLIC ${CMAKE_SOURCE_DIR}/include
)
target_link_libraries(qingying_command PUBLIC qingying_action)

add_library(qingying_mcp STATIC
  src/mcp/mcp_bridge.cpp
)
target_include_directories(qingying_mcp
  PUBLIC ${CMAKE_SOURCE_DIR}/include
)
target_link_libraries(qingying_mcp PUBLIC qingying_action)

add_executable(qingying WIN32
  src/app/main.cpp
  src/app/application.cpp
  src/app/single_instance_guard.cpp
  src/app/autostart_settings.cpp
  src/app/tray_controller.cpp
  res/qingying.rc
)
target_include_directories(qingying PRIVATE
  ${CMAKE_SOURCE_DIR}/include
  ${CMAKE_SOURCE_DIR}/res
)
target_link_libraries(qingying PRIVATE
  qingying_action
  qingying_capture
  qingying_export
  qingying_overlay
  qingying_annotate
  qingying_pin
  qingying_longshot
  qingying_command
  qingying_mcp
)
if(WIN32)
  target_link_libraries(qingying PRIVATE shell32 ole32 uuid advapi32)
endif()

# ---------------------------------------------------------------------------
# GoogleTest via vcpkg (relative to repo root)
#   <repo>/../thirdparty_install/vcpkg
# ---------------------------------------------------------------------------
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
