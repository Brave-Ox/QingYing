add_library(qingying_overlay STATIC
  mask_renderer.cpp
  overlay_phase.cpp
  overlay_renderer.cpp
  selection_controller.cpp
  selection_overlay.cpp
  selection_toolbar.cpp
  selection_handles.cpp
  coordinate_transform.cpp
  ${PROJECT_SOURCE_DIR}/src/window/smart_region_detector.cpp
  ${PROJECT_SOURCE_DIR}/src/window/known_content_locator.cpp
  ${PROJECT_SOURCE_DIR}/src/window/msaa_region_locator.cpp
  ${PROJECT_SOURCE_DIR}/src/window/uia_region_locator.cpp
  ${PROJECT_SOURCE_DIR}/src/window/uia_region_query_worker.cpp
  ${PROJECT_SOURCE_DIR}/src/window/visual_region_locator.cpp
)

target_include_directories(qingying_overlay
  PUBLIC ${PROJECT_SOURCE_DIR}/include
)

target_link_libraries(qingying_overlay
  PUBLIC qingying_action qingying_window
)

if(WIN32)
  target_link_libraries(qingying_overlay
    PUBLIC dwmapi
    PRIVATE gdi32 ole32 oleacc uiautomationcore user32 qingying_ui
  )
endif()
