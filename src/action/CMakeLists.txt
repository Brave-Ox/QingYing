add_library(qingying_action STATIC
  action_dispatcher.cpp
)

target_include_directories(qingying_action
  PUBLIC ${PROJECT_SOURCE_DIR}/include
)

# Migration-only boundary for the pre-typed all-fields action request. No
# production target links this library; callers must migrate to qingying_action.
add_library(qingying_action_compatibility STATIC
  compatibility/legacy_action_dispatcher.cpp
)

target_include_directories(qingying_action_compatibility
  PUBLIC ${PROJECT_SOURCE_DIR}/include
)

target_link_libraries(qingying_action_compatibility
  PUBLIC qingying_action
)
