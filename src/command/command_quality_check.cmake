# Command module quality gate. This script intentionally checks only files
# owned by the command module and its command-specific tests.

if(NOT DEFINED QINGYING_SOURCE_DIR)
  message(FATAL_ERROR "QINGYING_SOURCE_DIR is required")
endif()

set(command_files
  "${QINGYING_SOURCE_DIR}/src/command/CMakeLists.txt"
  "${QINGYING_SOURCE_DIR}/src/command/command_dimension_parser.cpp"
  "${QINGYING_SOURCE_DIR}/src/command/command_dimension_parser.hpp"
  "${QINGYING_SOURCE_DIR}/src/command/command_parser.cpp"
  "${QINGYING_SOURCE_DIR}/src/command/command_parse_diagnostics.cpp"
  "${QINGYING_SOURCE_DIR}/src/command/command_plan_explainer.cpp"
  "${QINGYING_SOURCE_DIR}/src/command/command_plan_validator.cpp"
  "${QINGYING_SOURCE_DIR}/src/command/command_rules.cpp"
  "${QINGYING_SOURCE_DIR}/src/command/command_rules.hpp"
  "${QINGYING_SOURCE_DIR}/src/command/command_text.cpp"
  "${QINGYING_SOURCE_DIR}/src/command/command_text.hpp"
  "${QINGYING_SOURCE_DIR}/include/qingying/command/command_parser.hpp"
  "${QINGYING_SOURCE_DIR}/include/qingying/command/command_parse_diagnostics.hpp"
  "${QINGYING_SOURCE_DIR}/include/qingying/command/command_plan_explainer.hpp"
  "${QINGYING_SOURCE_DIR}/include/qingying/command/command_plan_validator.hpp"
  "${QINGYING_SOURCE_DIR}/tests/command_dimension_parser_test.cpp"
  "${QINGYING_SOURCE_DIR}/tests/command_parser_diagnostics_test.cpp"
  "${QINGYING_SOURCE_DIR}/tests/command_parser_fuzz_test.cpp"
  "${QINGYING_SOURCE_DIR}/tests/command_parser_golden_test.cpp"
  "${QINGYING_SOURCE_DIR}/tests/command_parser_performance_test.cpp"
  "${QINGYING_SOURCE_DIR}/tests/command_parser_p0_regression_test.cpp"
  "${QINGYING_SOURCE_DIR}/tests/command_parser_test.cpp"
  "${QINGYING_SOURCE_DIR}/tests/command_text_test.cpp"
)

foreach(file_path IN LISTS command_files)
  if(NOT EXISTS "${file_path}")
    message(FATAL_ERROR "Command quality check cannot find ${file_path}")
  endif()

  file(READ "${file_path}" content HEX)
  string(TOLOWER "${content}" content)
  string(SUBSTRING "${content}" 0 6 bom)
  get_filename_component(extension "${file_path}" EXT)
  if(extension STREQUAL ".cmake" OR file_path MATCHES "CMakeLists\\.txt$")
    if(bom STREQUAL "efbbbf")
      message(FATAL_ERROR "CMake file must not have UTF-8 BOM: ${file_path}")
    endif()
  else()
    if(NOT bom STREQUAL "efbbbf")
      message(FATAL_ERROR "Source file must have UTF-8 BOM: ${file_path}")
    endif()
  endif()

  string(FIND "${content}" "0d0a" crlf_index)
  if(crlf_index EQUAL -1)
    message(FATAL_ERROR "File must use CRLF line endings: ${file_path}")
  endif()
  string(REPLACE "0d0a" "" content_without_crlf "${content}")
  string(FIND "${content_without_crlf}" "0a" bare_lf_index)
  if(NOT bare_lf_index EQUAL -1)
    message(FATAL_ERROR "File contains bare LF line endings: ${file_path}")
  endif()
endforeach()

message(STATUS "Command module quality check passed")
