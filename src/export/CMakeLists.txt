add_library(qingying_export STATIC
  dib_encoder.cpp
  export_service.cpp
)

target_include_directories(qingying_export
  PUBLIC ${PROJECT_SOURCE_DIR}/include
)

target_link_libraries(qingying_export
  PUBLIC qingying_action
)

if(WIN32)
  target_link_libraries(qingying_export
    PUBLIC windowscodecs ole32
  )
endif()
