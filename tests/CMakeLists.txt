# QingYing unit tests (GoogleTest via vcpkg)

add_executable(qingying_tests
  action_dispatcher_test.cpp
  action_handlers_test.cpp
  annotation_document_test.cpp
  annotation_editor_session_test.cpp
  annotation_engine_test.cpp
  annotation_interaction_controller_test.cpp
  annotation_overlay_test.cpp
  annotation_renderer_test.cpp
  longshot_request_adapter_test.cpp
  capture_engine_test.cpp
  capture_preview_test.cpp
  capture_session_test.cpp
  dib_encoder_test.cpp
  export_service_test.cpp
  mask_renderer_test.cpp
  modern_toolbar_test.cpp
  overlay_phase_test.cpp
  overlay_renderer_test.cpp
  selection_controller_test.cpp
  selection_handles_test.cpp
  selection_toolbar_test.cpp
  coordinate_transform_test.cpp
  f1_selection_integration_test.cpp
  pin_manager_test.cpp
  longshot_stitcher_test.cpp
  longshot_engine_test.cpp
  notepad_longshot_profile_test.cpp
  window_detector_test.cpp
  ${CMAKE_SOURCE_DIR}/src/app/action_handlers.cpp
  ${CMAKE_SOURCE_DIR}/src/app/capture_preview.cpp
)

target_include_directories(qingying_tests
  PRIVATE ${CMAKE_SOURCE_DIR}/include
)

target_link_libraries(qingying_tests
  PRIVATE
    qingying_action
    qingying_annotate
    qingying_capture
    qingying_export
    qingying_overlay
    qingying_ui
    qingying_pin
    qingying_longshot
    GTest::gtest
    GTest::gtest_main
)

# Console subsystem (not WIN32) so gtest prints to stdout
if(MSVC)
  target_compile_options(qingying_tests PRIVATE /utf-8)
endif()

include(GoogleTest)
gtest_discover_tests(qingying_tests
  WORKING_DIRECTORY $<TARGET_FILE_DIR:qingying_tests>
  DISCOVERY_MODE PRE_TEST
  DISCOVERY_TIMEOUT 60
)

# Ensure gtest DLLs are next to the test exe when using shared vcpkg libs
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
