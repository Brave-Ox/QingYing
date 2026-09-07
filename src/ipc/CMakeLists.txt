add_library(qingying_ipc STATIC automation_wire_codec.cpp)
target_include_directories(qingying_ipc PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(qingying_ipc
  PUBLIC qingying_automation_contract
  PRIVATE nlohmann_json::nlohmann_json
)
