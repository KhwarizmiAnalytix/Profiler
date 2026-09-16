# Phase 0 overhead-measurement tool (docs/design-review.md section 1.1;
# docs/benchmarking.md documents the methodology). Enable with
# -DPROFILER_ENABLE_BENCHMARKS=ON. Not built by default: this is a
# measurement tool, not something every consumer needs.
cmake_minimum_required(VERSION 3.16)

add_executable(profiler_benchmark profiler_benchmark.cpp)

if(PROFILER_CXX_STANDARD)
    set(_profiler_benchmark_cxx_std "${PROFILER_CXX_STANDARD}")
elseif(CMAKE_CXX_STANDARD)
    set(_profiler_benchmark_cxx_std "${CMAKE_CXX_STANDARD}")
else()
    set(_profiler_benchmark_cxx_std 20)
endif()
set_target_properties(
    profiler_benchmark PROPERTIES CXX_STANDARD ${_profiler_benchmark_cxx_std} CXX_STANDARD_REQUIRED
                                                                              ON CXX_EXTENSIONS OFF
)

target_link_libraries(profiler_benchmark PRIVATE Profiler::Profiler)

if(COMMAND profiler_set_target_output_directories)
    profiler_set_target_output_directories(profiler_benchmark)
else()
    set_target_properties(
        profiler_benchmark PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
    )
endif()

# Best-effort git commit for the machine/toolchain fingerprint each report
# prints (design-review.md: "store machine/toolchain/SDK configuration with
# benchmark data"). Empty string (not a build failure) when git or a repo
# isn't available, e.g. from a source tarball.
find_package(Git QUIET)
set(PROFILER_BENCHMARK_GIT_COMMIT "")
if(Git_FOUND)
    execute_process(
        COMMAND ${GIT_EXECUTABLE} rev-parse --short HEAD
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
        OUTPUT_VARIABLE PROFILER_BENCHMARK_GIT_COMMIT
        OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET
        RESULT_VARIABLE _profiler_benchmark_git_result
    )
    if(NOT _profiler_benchmark_git_result EQUAL 0)
        set(PROFILER_BENCHMARK_GIT_COMMIT "")
    endif()
endif()
target_compile_definitions(
    profiler_benchmark PRIVATE PROFILER_BENCHMARK_GIT_COMMIT="${PROFILER_BENCHMARK_GIT_COMMIT}"
)
