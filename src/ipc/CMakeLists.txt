add_library(qingying_ipc STATIC
  action_catalog_codec.cpp
  automation_wire_codec.cpp
  pipe_identity.cpp
  pipe_io.cpp
  pipe_server.cpp
  pipe_automation_client.cpp
)
target_include_directories(qingying_ipc PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(qingying_ipc
  PUBLIC qingying_automation_contract
  PRIVATE nlohmann_json::nlohmann_json advapi32
)
