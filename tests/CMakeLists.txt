# 轻映单元测试（通过 vcpkg 引入 GoogleTest）

add_executable(qingying_tests
  mcp_process_integration_test.cpp
  mcp_result_lifecycle_integration_test.cpp
  application_shutdown_coordinator_test.cpp
  automation_shutdown_test.cpp
  export_executor_test.cpp
  async_save_handler_test.cpp
  async_capture_service_test.cpp
  capture_service_test.cpp
  crop_center_handler_test.cpp
  capture_window_handler_test.cpp
  action_dispatcher_test.cpp
  action_handlers_test.cpp
  automation_contract_test.cpp
  operation_registry_test.cpp
  ui_action_scheduler_test.cpp
  automation_endpoint_test.cpp
  automation_wire_codec_test.cpp
  pipe_transport_test.cpp
  pipe_identity_test.cpp
  interaction_gate_test.cpp
  annotation_document_test.cpp
  annotation_editor_session_test.cpp
  annotation_engine_test.cpp
  annotation_font_catalog_test.cpp
  annotation_interaction_controller_test.cpp
  annotation_overlay_test.cpp
  annotation_renderer_test.cpp
  color_convert_test.cpp
  color_picker_state_test.cpp
  color_picker_surface_test.cpp
  longshot_request_adapter_test.cpp
  mcp_bridge_test.cpp
  mcp_protocol_session_test.cpp
  mcp_tool_catalog_test.cpp
  stdio_transport_test.cpp
  longshot_controller_test.cpp
  capture_engine_test.cpp
  capture_preview_test.cpp
  capture_workflow_test.cpp
  capture_session_test.cpp
  result_action_service_test.cpp
  save_policy_test.cpp
  png_file_transaction_test.cpp
  result_budget_test.cpp
  result_store_test.cpp
  ui_message_channel_test.cpp
  dib_encoder_test.cpp
  export_service_test.cpp
  mask_renderer_test.cpp
  modern_toolbar_test.cpp
  overlay_phase_test.cpp
  overlay_renderer_test.cpp
  selection_controller_test.cpp
  selection_overlay_test.cpp
  selection_handles_test.cpp
  selection_toolbar_test.cpp
  coordinate_transform_test.cpp
  f1_selection_integration_test.cpp
  pin_manager_test.cpp
  longshot_stitcher_test.cpp
  longshot_engine_test.cpp
  longshot_plugin_api_test.cpp
  longshot_plugin_host_test.cpp
  dll_longshot_profile_test.cpp
  builtin_longshot_plugin_test.cpp
  browser_longshot_profile_test.cpp
  notepad_longshot_profile_test.cpp
  explorer_longshot_profile_test.cpp
  window_detector_test.cpp
  window_resolver_test.cpp
  smart_region_detector_test.cpp
  smart_region_mode_test.cpp
  visual_region_locator_test.cpp
  tray_notify_icon_test.cpp
  tray_context_menu_test.cpp
)

add_library(qingying_test_longshot_plugin MODULE
  longshot_test_plugin.cpp
)
target_include_directories(qingying_test_longshot_plugin
  PRIVATE ${CMAKE_SOURCE_DIR}/include
)
target_compile_definitions(qingying_test_longshot_plugin
  PRIVATE QINGYING_LONGSHOT_PLUGIN_EXPORTS
)
set_target_properties(qingying_test_longshot_plugin PROPERTIES
  PREFIX ""
  OUTPUT_NAME "qingying_test_longshot_plugin"
)
add_dependencies(qingying_tests qingying_test_longshot_plugin)
add_custom_command(TARGET qingying_test_longshot_plugin POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
    $<TARGET_FILE:qingying_test_longshot_plugin>
    $<TARGET_FILE_DIR:qingying_tests>
  COMMENT "Copy test long-shot plugin next to qingying_tests"
)

add_library(qingying_blocking_legacy_longshot_plugin MODULE
  longshot_blocking_legacy_plugin.cpp
)
target_include_directories(qingying_blocking_legacy_longshot_plugin
  PRIVATE ${CMAKE_SOURCE_DIR}/include
)
target_compile_definitions(qingying_blocking_legacy_longshot_plugin
  PRIVATE QINGYING_LONGSHOT_PLUGIN_EXPORTS
)
set_target_properties(qingying_blocking_legacy_longshot_plugin PROPERTIES
  PREFIX ""
  OUTPUT_NAME "qingying_blocking_legacy_longshot_plugin"
)
add_dependencies(qingying_tests qingying_blocking_legacy_longshot_plugin)
add_custom_command(TARGET qingying_blocking_legacy_longshot_plugin POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
    $<TARGET_FILE:qingying_blocking_legacy_longshot_plugin>
    $<TARGET_FILE_DIR:qingying_tests>
  COMMENT "Copy blocking legacy long-shot plugin next to qingying_tests"
)

if(WIN32)
  add_dependencies(qingying_tests
    qingying_notepad_longshot_plugin
    qingying_explorer_longshot_plugin
    qingying_browser_longshot_plugin
  )
  add_custom_command(TARGET qingying_tests POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory
      "$<TARGET_FILE_DIR:qingying_tests>/plugins/longshot"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
      $<TARGET_FILE:qingying_notepad_longshot_plugin>
      "$<TARGET_FILE_DIR:qingying_tests>/plugins/longshot"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
      $<TARGET_FILE:qingying_explorer_longshot_plugin>
      "$<TARGET_FILE_DIR:qingying_tests>/plugins/longshot"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
      $<TARGET_FILE:qingying_browser_longshot_plugin>
      "$<TARGET_FILE_DIR:qingying_tests>/plugins/longshot"
    COMMENT "Copy built-in long-shot plugins next to qingying_tests"
  )
endif()

target_include_directories(qingying_tests
  PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/src/longshot
    ${CMAKE_SOURCE_DIR}/src/window
    ${CMAKE_SOURCE_DIR}/src/smart_region
    ${CMAKE_SOURCE_DIR}/src/ipc
    ${CMAKE_SOURCE_DIR}/src/mcp
)

target_link_libraries(qingying_tests
  PRIVATE
    qingying_action
    qingying_action_compatibility
    qingying_automation_contract
    qingying_automation
    qingying_ipc
    qingying_mcp
    nlohmann_json::nlohmann_json
    qingying_annotate
    qingying_capture
    qingying_export
    qingying_smart_region
    qingying_overlay
    qingying_ui
    qingying_pin
    qingying_longshot
    qingying_workflow
    qingying_app_handlers
    qingying_app_runtime
    GTest::gtest
    GTest::gtest_main
)

# 使用控制台子系统（不设置 WIN32），让 gtest 输出到标准输出
if(MSVC)
  target_compile_options(qingying_tests PRIVATE /utf-8)
endif()

include(GoogleTest)
add_executable(qingying_automation_core_tests
  automation_session_state_test.cpp
  automation_core_test.cpp
  ui_action_scheduler_test.cpp)
target_link_libraries(qingying_automation_core_tests PRIVATE
  qingying_automation GTest::gtest GTest::gtest_main)
if(WIN32)
  add_custom_command(TARGET qingying_automation_core_tests POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
      $<TARGET_FILE:GTest::gtest> $<TARGET_FILE_DIR:qingying_automation_core_tests>
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
      $<TARGET_FILE:GTest::gtest_main> $<TARGET_FILE_DIR:qingying_automation_core_tests>)
endif()
gtest_discover_tests(qingying_automation_core_tests
  TEST_PREFIX "AutomationCore."
  WORKING_DIRECTORY $<TARGET_FILE_DIR:qingying_automation_core_tests>
  DISCOVERY_MODE PRE_TEST)
get_target_property(QINGYING_DESKTOP_SOURCES qingying SOURCES)
list(TRANSFORM QINGYING_DESKTOP_SOURCES PREPEND "${PROJECT_SOURCE_DIR}/src/app/" REGEX "^[^/].*\\.cpp$")
add_executable(qingying_process_fixture WIN32 ${QINGYING_DESKTOP_SOURCES})
target_compile_definitions(qingying_process_fixture PRIVATE QINGYING_PROCESS_TESTING)
target_include_directories(qingying_process_fixture PRIVATE ${PROJECT_SOURCE_DIR}/include ${PROJECT_SOURCE_DIR}/res)
get_target_property(QINGYING_DESKTOP_LIBRARIES qingying LINK_LIBRARIES)
target_link_libraries(qingying_process_fixture PRIVATE ${QINGYING_DESKTOP_LIBRARIES})
add_dependencies(qingying_tests qingying qingying_process_fixture)

gtest_discover_tests(qingying_tests
  WORKING_DIRECTORY $<TARGET_FILE_DIR:qingying_tests>
  DISCOVERY_MODE PRE_TEST
  DISCOVERY_TIMEOUT 60
)

# 使用 vcpkg 共享库时，确保 gtest 的 DLL 与测试程序放在一起
if(WIN32)
  add_custom_command(TARGET qingying_tests POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
      $<TARGET_FILE:GTest::gtest>
      $<TARGET_FILE_DIR:qingying_tests>
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
      $<TARGET_FILE:GTest::gtest_main>
      $<TARGET_FILE_DIR:qingying_tests>
    COMMENT "Copy gtest DLLs next to qingying_tests"
  )
endif()

# SmartRegion tests deliberately link no overlay/application targets.
add_executable(qingying_smart_region_tests smart_region_boundary_test.cpp)
target_link_libraries(qingying_smart_region_tests PRIVATE
  qingying_smart_region GTest::gtest GTest::gtest_main)
if(WIN32)
  add_custom_command(TARGET qingying_smart_region_tests POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
      $<TARGET_FILE:GTest::gtest> $<TARGET_FILE_DIR:qingying_smart_region_tests>
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
      $<TARGET_FILE:GTest::gtest_main> $<TARGET_FILE_DIR:qingying_smart_region_tests>)
endif()
gtest_discover_tests(qingying_smart_region_tests
  WORKING_DIRECTORY $<TARGET_FILE_DIR:qingying_smart_region_tests>
  DISCOVERY_MODE PRE_TEST)
