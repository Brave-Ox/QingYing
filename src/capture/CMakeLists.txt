add_library(qingying_capture STATIC
  capture_engine.cpp
)

target_include_directories(qingying_capture
  PUBLIC ${PROJECT_SOURCE_DIR}/include
)

target_link_libraries(qingying_capture
  PUBLIC qingying_action
)

if(WIN32)
  target_link_libraries(qingying_capture
    PRIVATE d3d11 dxgi
  )
endif()
