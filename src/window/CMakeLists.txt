add_library(qingying_window STATIC
  window_catalog.cpp
  window_detector.cpp
  window_resolver.cpp
)
target_include_directories(qingying_window PUBLIC ${PROJECT_SOURCE_DIR}/include
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(qingying_window PUBLIC qingying_action)
if(WIN32)
  target_link_libraries(qingying_window PRIVATE dwmapi user32)
endif()
