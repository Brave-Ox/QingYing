# Project-wide compiler settings.

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(MSVC)
  # The project intentionally catches allocation, IPC and UI-boundary failures.
  # /EHsc is required for deterministic stack unwinding through those paths.
  add_compile_options(/utf-8 /W4 /EHsc)
endif()
