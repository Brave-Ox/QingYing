add_library(qingying_command STATIC
  command_parser.cpp
  command_dimension_parser.cpp
  command_parse_diagnostics.cpp
  command_plan_explainer.cpp
  command_plan_validator.cpp
  command_rules.cpp
  command_text.cpp
)

target_include_directories(qingying_command
  PUBLIC ${PROJECT_SOURCE_DIR}/include
)

target_link_libraries(qingying_command
  PUBLIC qingying_action
)

add_custom_target(qingying_command_quality
  COMMAND ${CMAKE_COMMAND}
    -DQINGYING_SOURCE_DIR=${PROJECT_SOURCE_DIR}
    -P ${CMAKE_CURRENT_SOURCE_DIR}/command_quality_check.cmake
  COMMENT "Check command module encoding and line endings"
  VERBATIM
)
