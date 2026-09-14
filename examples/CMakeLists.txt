# Standalone examples. example_quickstart.cpp is the integration any other
# C++ repo should copy. Enable with -DPROFILER_ENABLE_EXAMPLES=ON.
cmake_minimum_required(VERSION 3.16)

set(example_sources example_quickstart.cpp example_reports.cpp example_profiling_basic.cpp)
if(PROFILER_ENABLE_KINETO)
  list(APPEND example_sources example_hta.cpp)
endif()

# =============================================================================
# Build Examples
# =============================================================================
foreach(example_source ${example_sources})
  # Extract example name from filename (remove .cpp extension)
  string(REPLACE ".cpp" "" example_name ${example_source})

  # Create executable
  add_executable(${example_name} ${example_source})

  if(PROFILER_CXX_STANDARD)
    set(_example_cxx_std "${PROFILER_CXX_STANDARD}")
  elseif(CMAKE_CXX_STANDARD)
    set(_example_cxx_std "${CMAKE_CXX_STANDARD}")
  else()
    set(_example_cxx_std 20)
  endif()
  set_target_properties(
    ${example_name} PROPERTIES CXX_STANDARD ${_example_cxx_std} CXX_STANDARD_REQUIRED ON
                               CXX_EXTENSIONS OFF
  )

  target_link_libraries(${example_name} PRIVATE Profiler::Profiler)

  # Set output directory
  if(COMMAND profiler_set_target_output_directories)
    profiler_set_target_output_directories(${example_name})
  else()
    set_target_properties(
      ${example_name} PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
    )
  endif()

  # Add to Examples folder in IDE
  set_target_properties(${example_name} PROPERTIES FOLDER "Examples/Profiling")

  # Apply compiler flags if available
  if(MSVC)
    target_compile_options(${example_name} PRIVATE /WX)
  endif()

  message(STATUS "✓ Example configured: ${example_name}")

  if(PROFILER_ENABLE_TESTING AND NOT example_name STREQUAL "example_profiling_basic")
    set(_example_work_dir "${CMAKE_CURRENT_BINARY_DIR}/${example_name}-output")
    file(MAKE_DIRECTORY "${_example_work_dir}")
    add_test(NAME ${example_name} COMMAND ${example_name})
    set_tests_properties(${example_name} PROPERTIES
      WORKING_DIRECTORY "${_example_work_dir}" TIMEOUT 30 LABELS "Profiler;Example")
  endif()
endforeach()

message(STATUS "✓ Profiling Examples configured successfully")
