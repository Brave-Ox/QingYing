add_library(qingying_overlay STATIC
  mask_renderer.cpp
  overlay_phase.cpp
  overlay_renderer.cpp
  selection_controller.cpp
  selection_overlay.cpp
  selection_toolbar.cpp
  selection_handles.cpp
  coordinate_transform.cpp
  ${PROJECT_SOURCE_DIR}/src/window/window_detector.cpp
)

target_include_directories(qingying_overlay
  PUBLIC ${PROJECT_SOURCE_DIR}/include
)

target_link_libraries(qingying_overlay
  PUBLIC qingying_action
)

if(WIN32)
  target_link_libraries(qingying_overlay
    PUBLIC dwmapi
    PRIVATE gdi32 user32 qingying_ui
  )
endif()
