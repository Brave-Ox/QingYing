add_library(qingying_longshot STATIC
  longshot_engine.cpp
  image_stitcher.cpp
  dll_longshot_profile.cpp
  longshot_plugin_host.cpp
  longshot_profile.cpp
  longshot_profile_registry.cpp
  win32_scroll_helpers.cpp
  generic_scroll_target_resolver.cpp
  generic_wheel_longshot_profile.cpp
  visual_frame_settler.cpp
  browser_longshot_resolver.cpp
)

target_include_directories(qingying_longshot
  PUBLIC ${PROJECT_SOURCE_DIR}/include
)

target_link_libraries(qingying_longshot
  PUBLIC qingying_action qingying_capture
)

if(WIN32)
  target_link_libraries(qingying_longshot
    PRIVATE user32
  )

  # Built-in application adapters are standalone DLLs. The application links
  # only the generic long-shot runtime and discovers these plugins at runtime.
  function(qingying_configure_longshot_plugin target output_name)
    target_include_directories(${target}
      PRIVATE
        ${PROJECT_SOURCE_DIR}/include
        ${CMAKE_CURRENT_SOURCE_DIR}
    )
    target_compile_definitions(${target}
      PRIVATE QINGYING_LONGSHOT_PLUGIN_EXPORTS
    )
    target_link_libraries(${target}
      PRIVATE user32
    )
    set_target_properties(${target} PROPERTIES
      PREFIX ""
      OUTPUT_NAME "${output_name}"
      RUNTIME_OUTPUT_DIRECTORY "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}"
    )
  endfunction()

  add_library(qingying_notepad_longshot_plugin MODULE
    notepad_longshot_plugin.cpp
    builtin_longshot_plugin_support.cpp
    notepad_longshot_resolver.cpp
    longshot_profile.cpp
  )
  qingying_configure_longshot_plugin(
    qingying_notepad_longshot_plugin
    qingying_notepad_longshot_plugin
  )

  add_library(qingying_explorer_longshot_plugin MODULE
    explorer_longshot_plugin.cpp
    builtin_longshot_plugin_support.cpp
    explorer_longshot_resolver.cpp
    longshot_profile.cpp
  )
  qingying_configure_longshot_plugin(
    qingying_explorer_longshot_plugin
    qingying_explorer_longshot_plugin
  )

  add_library(qingying_browser_longshot_plugin MODULE
    browser_longshot_plugin.cpp
    browser_longshot_resolver.cpp
    builtin_longshot_plugin_support.cpp
    longshot_profile.cpp
  )
  qingying_configure_longshot_plugin(
    qingying_browser_longshot_plugin
    qingying_browser_longshot_plugin
  )

  # app 仅调用这一通用入口，不再维护内置长截图插件的具体依赖。
  function(qingying_deploy_longshot_runtime application_target)
    add_dependencies(${application_target}
      qingying_notepad_longshot_plugin
      qingying_explorer_longshot_plugin
      qingying_browser_longshot_plugin
    )
    add_custom_command(TARGET ${application_target} POST_BUILD
      COMMAND ${CMAKE_COMMAND} -E make_directory
        "$<TARGET_FILE_DIR:${application_target}>/plugins/longshot"
      COMMAND ${CMAKE_COMMAND} -E copy_if_different
        $<TARGET_FILE:qingying_notepad_longshot_plugin>
        "$<TARGET_FILE_DIR:${application_target}>/plugins/longshot"
      COMMAND ${CMAKE_COMMAND} -E copy_if_different
        $<TARGET_FILE:qingying_explorer_longshot_plugin>
        "$<TARGET_FILE_DIR:${application_target}>/plugins/longshot"
      COMMAND ${CMAKE_COMMAND} -E copy_if_different
        $<TARGET_FILE:qingying_browser_longshot_plugin>
        "$<TARGET_FILE_DIR:${application_target}>/plugins/longshot"
      COMMENT "Deploy built-in long-shot plugins next to ${application_target}"
    )
  endfunction()
endif()
