# Optional --coverage instrumentation (GCC/Clang gcov-compatible).
#
# Usage: enable with -DPROFILER_ENABLE_COVERAGE=ON and a Debug-like build type
# (optimizations skew line coverage and can hide branches). See the "Code
# coverage" section of docs/profiler.md for the full local workflow.

function(profiler_enable_coverage target)
  if(NOT (CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang"))
    message(WARNING "PROFILER_ENABLE_COVERAGE has no effect with ${CMAKE_CXX_COMPILER_ID}; "
                     "coverage instrumentation requires GCC or Clang"
    )
    return()
  endif()
  if(CMAKE_BUILD_TYPE AND NOT CMAKE_BUILD_TYPE MATCHES "^(Debug|)$")
    message(WARNING "PROFILER_ENABLE_COVERAGE with CMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE}: "
                     "optimizations can skew line/branch coverage; Debug is recommended"
    )
  endif()
  target_compile_options(${target} PRIVATE --coverage -fno-inline -fno-elide-constructors)
  target_link_options(${target} PRIVATE --coverage)
endfunction()
