add_library(qingying_smart_region STATIC
  browser_shell_atlas.cpp
  browser_shell_policy.cpp
  smart_region_detector.cpp
  smart_region_diagnostics.cpp
  smart_region_mode.cpp
  known_content_locator.cpp
  msaa_region_locator.cpp
  uia_region_locator.cpp
  uia_region_query_worker.cpp
  visual_region_locator.cpp
)
target_include_directories(qingying_smart_region
  PUBLIC ${PROJECT_SOURCE_DIR}/include
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
)
target_link_libraries(qingying_smart_region PUBLIC qingying_window qingying_action)
if(WIN32)
  target_link_libraries(qingying_smart_region PRIVATE gdi32 ole32 oleacc uiautomationcore user32)
endif()
