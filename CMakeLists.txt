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
  src/export/dib_encoder.cpp
  src/export/export_service.cpp
)
target_include_directories(qingying_export
  PUBLIC ${CMAKE_SOURCE_DIR}/include
)
target_link_libraries(qingying_export PUBLIC qingying_action)
if(WIN32)
  target_link_libraries(qingying_export PUBLIC windowscodecs ole32)
endif()

add_library(qingying_ui STATIC
  src/ui/modern_toolbar.cpp
)
target_include_directories(qingying_ui
  PUBLIC ${CMAKE_SOURCE_DIR}/include
)
if(WIN32)
  target_link_libraries(qingying_ui PUBLIC gdi32 user32 comctl32 gdiplus)
endif()

add_library(qingying_overlay STATIC
  src/overlay/mask_renderer.cpp
  src/overlay/overlay_phase.cpp
  src/overlay/overlay_renderer.cpp
  src/overlay/selection_controller.cpp
  src/overlay/selection_overlay.cpp
  src/overlay/selection_toolbar.cpp
  src/overlay/selection_handles.cpp
  src/overlay/coordinate_transform.cpp
  src/window/window_detector.cpp
)
target_include_directories(qingying_overlay
  PUBLIC ${CMAKE_SOURCE_DIR}/include
)
target_link_libraries(qingying_overlay PUBLIC qingying_action)
if(WIN32)
  target_link_libraries(qingying_overlay PUBLIC dwmapi)
  target_link_libraries(qingying_overlay PRIVATE gdi32 user32 qingying_ui)
endif()

add_library(qingying_annotate STATIC
  src/annotate/annotation_engine.cpp
  src/annotate/annotation_document.cpp
  src/annotate/annotation_editor_session.cpp
  src/annotate/annotation_interaction_controller.cpp
  src/annotate/annotation_overlay.cpp
  src/annotate/annotation_editor_host.cpp
  src/annotate/annotation_inline_text.cpp
  src/annotate/annotation_editor_chrome.cpp
  src/annotate/annotation_stroke_popup.cpp
  src/annotate/annotation_color_picker.cpp
  src/annotate/annotation_editor_paint.cpp
  src/annotate/annotation_renderer.cpp
)
target_include_directories(qingying_annotate
  PUBLIC ${CMAKE_SOURCE_DIR}/include
  PRIVATE ${CMAKE_SOURCE_DIR}/src
)
target_link_libraries(qingying_annotate PUBLIC qingying_action qingying_ui)
if(WIN32)
  target_link_libraries(qingying_annotate PRIVATE gdi32 user32 comctl32)
endif()

add_library(qingying_pin STATIC
  src/pin/pin_manager.cpp
  src/pin/pin_window.cpp
)
target_include_directories(qingying_pin
  PUBLIC ${CMAKE_SOURCE_DIR}/include
)
target_link_libraries(qingying_pin PUBLIC qingying_action)
if(WIN32)
  target_link_libraries(qingying_pin PUBLIC user32 gdi32 dwmapi)
endif()

add_library(qingying_longshot STATIC
  src/longshot/longshot_engine.cpp
  src/longshot/image_stitcher.cpp
  src/longshot/longshot_profile.cpp
  src/longshot/longshot_profile_registry.cpp
  src/longshot/notepad_longshot_profile.cpp
  src/longshot/explorer_longshot_profile.cpp
  src/longshot/win32_scroll_helpers.cpp
)
target_include_directories(qingying_longshot
  PUBLIC ${CMAKE_SOURCE_DIR}/include
)
target_link_libraries(qingying_longshot PUBLIC qingying_action qingying_capture)
if(WIN32)
  target_link_libraries(qingying_longshot PRIVATE user32)
endif()

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

add_library(qingying_workflow STATIC
  src/app/capture_preview.cpp
  src/app/capture_workflow.cpp
  src/app/longshot_controller.cpp
)
target_include_directories(qingying_workflow
  PUBLIC ${CMAKE_SOURCE_DIR}/include
)
target_link_libraries(qingying_workflow
  PUBLIC
    qingying_action
    qingying_overlay
  PRIVATE
    qingying_annotate
    qingying_capture
    qingying_export
    qingying_longshot
    qingying_pin
)
if(WIN32)
  target_link_libraries(qingying_workflow PRIVATE comdlg32 user32)
endif()

add_executable(qingying WIN32
  src/app/main.cpp
  src/app/application.cpp
  src/app/single_instance_guard.cpp
  src/app/autostart_settings.cpp
  src/app/tray_controller.cpp
  src/app/hotkey_manager.cpp
  src/app/action_handlers.cpp
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
  qingying_ui
  qingying_pin
  qingying_longshot
  qingying_command
  qingying_mcp
  qingying_workflow
)
if(WIN32)
  target_link_libraries(qingying PRIVATE shell32 ole32 uuid advapi32 comdlg32 gdi32)
endif()

# ---------------------------------------------------------------------------
# 通过 vcpkg 引入 GoogleTest（相对于仓库根目录）
#   <repo>/../thirdparty_install/vcpkg  （仓库同级目录，不入库）
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
