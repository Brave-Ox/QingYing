add_library(qingying_mcp STATIC
  mcp_bridge.cpp
  mcp_protocol_session.cpp
  stdio_transport.cpp
  tool_catalog.cpp
)

target_include_directories(qingying_mcp
  PUBLIC ${PROJECT_SOURCE_DIR}/include
  PRIVATE ${PROJECT_SOURCE_DIR}/src/ipc
)

target_link_libraries(qingying_mcp
  PUBLIC qingying_automation_contract
  PRIVATE qingying_ipc nlohmann_json::nlohmann_json
)
