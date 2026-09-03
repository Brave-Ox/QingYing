add_library(qingying_pin STATIC
  pin_manager.cpp
  pin_window.cpp
)

target_include_directories(qingying_pin
  PUBLIC ${PROJECT_SOURCE_DIR}/include
)

target_link_libraries(qingying_pin
  PUBLIC qingying_action
)

if(WIN32)
  target_link_libraries(qingying_pin
    PUBLIC user32 gdi32 dwmapi
  )
endif()
