add_library(qingying_workflow STATIC
  capture_preview.cpp
  capture_workflow.cpp
  longshot_controller.cpp
  browser_capture_receiver.cpp
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

  add_dependencies(qingying
    qingying_notepad_longshot_plugin
    qingying_explorer_longshot_plugin
    qingying_chrome_longshot_plugin
  )

  add_custom_command(TARGET qingying POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory
      "$<TARGET_FILE_DIR:qingying>/plugins/longshot"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
      $<TARGET_FILE:qingying_notepad_longshot_plugin>
      "$<TARGET_FILE_DIR:qingying>/plugins/longshot"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
      $<TARGET_FILE:qingying_explorer_longshot_plugin>
      "$<TARGET_FILE_DIR:qingying>/plugins/longshot"
    COMMENT "Deploy built-in long-shot plugins next to qingying"
  )
endif()
