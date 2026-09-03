add_library(qingying_action STATIC
  action_dispatcher.cpp
)

target_include_directories(qingying_action
  PUBLIC ${PROJECT_SOURCE_DIR}/include
)
