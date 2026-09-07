# Neutral values and client/completion contracts; no transport or GUI runtime.
add_library(qingying_automation_contract INTERFACE)

target_include_directories(qingying_automation_contract
  INTERFACE ${PROJECT_SOURCE_DIR}/include
)
target_link_libraries(qingying_automation_contract
  INTERFACE qingying_action
)
target_compile_features(qingying_automation_contract INTERFACE cxx_std_17)
