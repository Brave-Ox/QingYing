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
  capture_engine_test.cpp
  capture_session_test.cpp
  dib_encoder_test.cpp
  export_service_test.cpp
  mask_renderer_test.cpp
  modern_toolbar_test.cpp
  selection_controller_test.cpp
  ${CMAKE_SOURCE_DIR}/src/app/action_handlers.cpp
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
