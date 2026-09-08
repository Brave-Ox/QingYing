add_library(qingying_annotate STATIC
  annotation_engine.cpp
  annotation_font_catalog.cpp
  annotation_document.cpp
  annotation_editor_session.cpp
  annotation_interaction_controller.cpp
  annotation_overlay.cpp
  annotation_editor_host.cpp
  annotation_inline_text.cpp
  annotation_editor_chrome.cpp
  annotation_stroke_popup.cpp
  annotation_color_picker.cpp
  annotation_editor_paint.cpp
  annotation_renderer.cpp
  annotation_text_rasterizer.cpp
)

target_include_directories(qingying_annotate
  PUBLIC ${PROJECT_SOURCE_DIR}/include
  PRIVATE ${PROJECT_SOURCE_DIR}/src
)

target_link_libraries(qingying_annotate
  PUBLIC qingying_action qingying_ui
  PRIVATE qingying_capture
)

if(WIN32)
  target_link_libraries(qingying_annotate
    PRIVATE gdi32 user32 comctl32 gdiplus
  )
endif()
