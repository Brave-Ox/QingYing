add_library(qingying_mcp STATIC
  mcp_bridge.cpp
)

target_include_directories(qingying_mcp
  PUBLIC ${PROJECT_SOURCE_DIR}/include
)

target_link_libraries(qingying_mcp
  PUBLIC qingying_action
)
