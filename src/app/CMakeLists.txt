add_library(qingying_workflow STATIC
  capture_preview.cpp
  capture_service.cpp
  capture_workflow.cpp
  export_executor.cpp
  longshot_controller.cpp
  save_policy.cpp
  result_action_service.cpp
  result_budget.cpp
  result_store.cpp
)

target_include_directories(qingying_workflow
  PUBLIC ${PROJECT_SOURCE_DIR}/include
)

target_link_libraries(qingying_workflow
  PUBLIC
    qingying_action
    qingying_overlay
  PRIVATE
    qingying_annotate
    qingying_capture
    qingying_export
    qingying_longshot
    qingying_pin
)

if(WIN32)
  target_link_libraries(qingying_workflow
    PRIVATE comdlg32 user32
  )
endif()

add_library(qingying_app_handlers STATIC
  action_handlers.cpp
  async_save_handler.cpp
)

target_include_directories(qingying_app_handlers
  PUBLIC ${PROJECT_SOURCE_DIR}/include
)

target_link_libraries(qingying_app_handlers
  PUBLIC qingying_action
  PRIVATE
    qingying_capture
    qingying_window
    qingying_workflow
)

add_library(qingying_app_runtime STATIC
  automation_runtime.cpp
  automation_settings.cpp
  mcp_stdio_runner.cpp
)
target_include_directories(qingying_app_runtime PUBLIC ${PROJECT_SOURCE_DIR}/include)
target_link_libraries(qingying_app_runtime PUBLIC qingying_automation qingying_ipc qingying_mcp PRIVATE advapi32)

add_executable(qingying WIN32
  main.cpp
  application.cpp
  single_instance_guard.cpp
  autostart_settings.cpp
  tray_controller.cpp
  hotkey_manager.cpp
  ${PROJECT_SOURCE_DIR}/res/qingying.rc
)

target_include_directories(qingying
  PRIVATE
    ${PROJECT_SOURCE_DIR}/include
    ${PROJECT_SOURCE_DIR}/res
)

target_link_libraries(qingying
  PRIVATE
    qingying_action
    qingying_capture
    qingying_export
    qingying_overlay
    qingying_annotate
    qingying_ui
    qingying_pin
    qingying_longshot
    qingying_command
    qingying_mcp
    qingying_workflow
    qingying_app_handlers
    qingying_automation
    qingying_app_runtime
)

if(WIN32)
  target_link_libraries(qingying
    PRIVATE shell32 ole32 uuid advapi32 comdlg32 gdi32
  )

  qingying_deploy_longshot_runtime(qingying)
endif()
