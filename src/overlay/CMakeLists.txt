add_library(qingying_overlay STATIC
  mask_renderer.cpp
  overlay_phase.cpp
  overlay_renderer.cpp
  selection_controller.cpp
  selection_overlay.cpp
  selection_toolbar.cpp
  selection_handles.cpp
  coordinate_transform.cpp
)

target_include_directories(qingying_overlay
  PUBLIC ${PROJECT_SOURCE_DIR}/include
  PRIVATE ${PROJECT_SOURCE_DIR}/src/smart_region
)

target_link_libraries(qingying_overlay
  PUBLIC qingying_action qingying_window
  PRIVATE qingying_smart_region
)

if(WIN32)
  target_link_libraries(qingying_overlay
    PUBLIC dwmapi
    PRIVATE gdi32 user32 qingying_ui
  )
endif()
