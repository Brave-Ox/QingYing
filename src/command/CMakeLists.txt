add_library(qingying_command STATIC
  command_parser.cpp
)

target_include_directories(qingying_command
  PUBLIC ${PROJECT_SOURCE_DIR}/include
)

target_link_libraries(qingying_command
  PUBLIC qingying_action
)
