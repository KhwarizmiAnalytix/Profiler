# All user-facing Profiler build options, gathered in one place. Configure
# via -D<NAME>=<VALUE>, ccmake, or cmake-gui; see the "Build options" table in
# docs/profiler.md for the full reference.
#
# Derived/internal flags (PROFILER_HAS_*, PROFILER_ENABLE_KINETO/ITT,
# PROFILER_ENABLE_NATIVE_PROFILER, PROFILER_DEPENDENCY_*, …) are computed from
# these in CMakeLists.txt and are not meant to be set directly.

if(PROFILER_STANDALONE)
    option(BUILD_SHARED_LIBS "Build shared libraries" ON)
endif()

set(PROFILER_BACKEND "KINETO" CACHE STRING "Instrumentation backend: KINETO or ITT")
set_property(CACHE PROFILER_BACKEND PROPERTY STRINGS KINETO ITT)
if(NOT PROFILER_BACKEND MATCHES "^(KINETO|ITT)$")
    message(FATAL_ERROR "PROFILER_BACKEND must be KINETO or ITT")
endif()

# A parent project's MEMORY_GPU_BACKEND supplies the default if
# PROFILER_GPU_BACKEND is unset, so a host repo only has to set one GPU
# backend option for both modules.
if(NOT DEFINED PROFILER_GPU_BACKEND)
    if(DEFINED MEMORY_GPU_BACKEND)
        set(_profiler_gpu_default "${MEMORY_GPU_BACKEND}")
    else()
        set(_profiler_gpu_default "none")
    endif()
else()
    set(_profiler_gpu_default "${PROFILER_GPU_BACKEND}")
endif()
set(PROFILER_GPU_BACKEND "${_profiler_gpu_default}" CACHE STRING "GPU backend: none, cuda, hip")
set_property(CACHE PROFILER_GPU_BACKEND PROPERTY STRINGS none cuda hip)
unset(_profiler_gpu_default)
if(NOT PROFILER_GPU_BACKEND MATCHES "^(none|cuda|hip)$")
    message(FATAL_ERROR
            "PROFILER_GPU_BACKEND must be none, cuda, or hip (Metal support was removed)"
    )
endif()

option(PROFILER_ENABLE_TESTING "Build Profiler test suite" ON)
option(PROFILER_ENABLE_EXAMPLES "Build Profiler example programs" OFF)
option(PROFILER_ENABLE_LIBTORCH "Enable LibTorch in ProfilerCxxTests" OFF)
option(PROFILER_ENABLE_INSTALL "Install headers and CMake package" ${PROFILER_STANDALONE})
option(PROFILER_REQUIRE_CUDA "Fail configure if CUDA was requested but not found" OFF)
option(PROFILER_REQUIRE_NVTX "Fail configure if NVTX is missing" OFF)
option(PROFILER_ENABLE_COVERAGE "Instrument the Profiler library with --coverage (GCC/Clang)" OFF)

set(PROFILER_SANITIZER
    ""
    CACHE
        STRING
        "Sanitizer(s) to build with (GCC/Clang), e.g. address, undefined, address,undefined, thread"
)

set(PROFILER_CXX_STANDARD "20" CACHE STRING "C++ standard")

set(PROFILER_THIRD_PARTY_DIR ""
    CACHE PATH "Directory containing vendored fmt/, kineto/, ittapi/, googletest/ sources; \
defaults to <repo>/third_party when unset"
)
