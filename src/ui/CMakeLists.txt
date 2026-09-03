add_library(qingying_ui STATIC
  modern_toolbar.cpp
)

target_include_directories(qingying_ui
  PUBLIC ${PROJECT_SOURCE_DIR}/include
)

if(WIN32)
  target_link_libraries(qingying_ui
    PUBLIC gdi32 user32 comctl32 gdiplus
  )
endif()
