# =============================================================================
# XSigma
# Profiling Examples CMakeLists.txt
# =============================================================================
# This CMakeLists.txt configures and builds the XSigma Profiling example programs that
# demonstrate the usage of profiling APIs.
#
# Examples included: - example_profiling_basic: Comprehensive profiling example with XSigma,
# Kineto, and ITT
#
# Build these examples with: python setup.py config.build.examples.ninja.clang python setup.py
# config.build.examples.vs22
#
# Or manually: cmake -DXSIGMA_ENABLE_EXAMPLES=ON .. cmake --build .
# =============================================================================
cmake_minimum_required(VERSION 3.16)

# =============================================================================
# Example Source
# Files
# =============================================================================
set(example_sources example_profiling_basic.cpp)

# =============================================================================
# Build Examples
# =============================================================================
foreach(example_source ${example_sources})
  # Extract example name from filename (remove .cpp extension)
  string(REPLACE ".cpp" "" example_name ${example_source})

  # Create executable
  add_executable(${example_name} ${example_source})

  # XSIGMA_CXX_STANDARD is optional (root may only set CMAKE_CXX_STANDARD).
  if(XSIGMA_CXX_STANDARD)
    set(_example_cxx_std "${XSIGMA_CXX_STANDARD}")
  elseif(CMAKE_CXX_STANDARD)
    set(_example_cxx_std "${CMAKE_CXX_STANDARD}")
  else()
    set(_example_cxx_std 20)
  endif()
  set_target_properties(
    ${example_name} PROPERTIES CXX_STANDARD ${_example_cxx_std} CXX_STANDARD_REQUIRED ON
                               CXX_EXTENSIONS OFF
  )

  # Profiling example uses APIs from Profiler; link Core as well for static builds (PRIVATE Core on
  # Profiler).
  target_link_libraries(${example_name} PRIVATE Profiler::Profiler)

  # Set output directory
  if(COMMAND xsigma_set_target_output_directories)
    xsigma_set_target_output_directories(${example_name})
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
endforeach()

message(STATUS "✓ Profiling Examples configured successfully")
