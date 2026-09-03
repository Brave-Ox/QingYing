add_library(qingying_workflow STATIC
  capture_preview.cpp
  capture_workflow.cpp
  longshot_controller.cpp
  result_action_service.cpp
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

add_executable(qingying WIN32
  main.cpp
  application.cpp
  single_instance_guard.cpp
  autostart_settings.cpp
  tray_controller.cpp
  hotkey_manager.cpp
  action_handlers.cpp
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
)

if(WIN32)
  target_link_libraries(qingying
    PRIVATE shell32 ole32 uuid advapi32 comdlg32 gdi32
  )

  qingying_deploy_longshot_runtime(qingying)
endif()
