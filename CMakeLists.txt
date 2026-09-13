# Profiler — standalone CMake package. Any C++ project can consume it;
# XSigma is one consumer, not a required host.
cmake_minimum_required(VERSION 3.22)

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
  project(
    Profiler
    VERSION 1.0.0
    DESCRIPTION "Standalone C++ CPU/GPU profiler"
    HOMEPAGE_URL "https://github.com/KhwarizmiAnalytix/Profiler"
    LANGUAGES CXX C
  )
  set(PROFILER_STANDALONE ON)
  include(CTest)
  enable_testing()
  option(BUILD_SHARED_LIBS "Build shared libraries" ON)
  set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
  set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib")
  set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib")
else()
  set(PROFILER_STANDALONE OFF)
endif()

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(ProfilerDependencies)

set(PROFILER_BACKEND "KINETO" CACHE STRING "Instrumentation backend: KINETO or ITT")
set_property(CACHE PROFILER_BACKEND PROPERTY STRINGS KINETO ITT)
if(NOT PROFILER_BACKEND MATCHES "^(KINETO|ITT)$")
  message(FATAL_ERROR "PROFILER_BACKEND must be KINETO or ITT")
endif()

if(NOT DEFINED PROFILER_GPU_BACKEND)
  if(DEFINED MEMORY_GPU_BACKEND)
    set(_profiler_gpu_default "${MEMORY_GPU_BACKEND}")
  else()
    set(_profiler_gpu_default "none")
  endif()
else()
  set(_profiler_gpu_default "${PROFILER_GPU_BACKEND}")
endif()
set(PROFILER_GPU_BACKEND "${_profiler_gpu_default}"
    CACHE STRING "GPU backend: none, cuda, hip, metal"
)
set_property(CACHE PROFILER_GPU_BACKEND PROPERTY STRINGS none cuda hip metal)
unset(_profiler_gpu_default)

option(PROFILER_ENABLE_TESTING "Build Profiler test suite" ON)
option(PROFILER_ENABLE_EXAMPLES "Build Profiler example programs" OFF)
option(PROFILER_ENABLE_LIBTORCH "Enable LibTorch in ProfilerCxxTests" OFF)
option(PROFILER_ENABLE_INSTALL "Install headers and CMake package" ${PROFILER_STANDALONE})
option(PROFILER_REQUIRE_CUDA "Fail configure if CUDA was requested but not found" OFF)
option(PROFILER_REQUIRE_NVTX "Fail configure if NVTX is missing" OFF)

set(PROFILER_CXX_STANDARD "20" CACHE STRING "C++ standard")
set(CMAKE_CXX_STANDARD ${PROFILER_CXX_STANDARD})
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(PROFILER_BACKEND STREQUAL "KINETO")
  set(PROFILER_ENABLE_KINETO ON)
  set(PROFILER_ENABLE_ITT OFF)
else()
  set(PROFILER_ENABLE_KINETO OFF)
  set(PROFILER_ENABLE_ITT ON)
endif()
set(PROFILER_ENABLE_NATIVE_PROFILER ON CACHE BOOL "Native pipeline is always compiled" FORCE)

set(PROFILER_DEPENDENCY_LIBS "")
set(PROFILER_DEPENDENCY_INCLUDE_DIRS "")
set(PROFILER_DEPENDENCY_COMPILE_DEFINITIONS "")

set(PROFILER_HAS_METAL 0)
if(APPLE AND PROFILER_GPU_BACKEND STREQUAL "metal")
  set(PROFILER_HAS_METAL 1)
endif()
list(APPEND PROFILER_DEPENDENCY_COMPILE_DEFINITIONS PROFILER_HAS_METAL=${PROFILER_HAS_METAL})

set(BUILD_TESTING OFF CACHE BOOL "Disable third-party tests" FORCE)
set(BUILD_EXAMPLES OFF CACHE BOOL "Disable third-party examples" FORCE)
set(BUILD_BENCHMARKS OFF CACHE BOOL "Disable third-party benchmarks" FORCE)

set(PROFILER_HAS_CUDA 0)
set(PROFILER_HAS_NVTX 0)
set(PROFILER_HAS_HIP 0)

if(PROFILER_GPU_BACKEND STREQUAL "cuda")
  find_package(CUDAToolkit QUIET)
  if(CUDAToolkit_FOUND)
    set(PROFILER_HAS_CUDA 1)
    list(APPEND PROFILER_DEPENDENCY_LIBS CUDA::cudart)
    if(TARGET CUDA::nvToolsExt)
      list(APPEND PROFILER_DEPENDENCY_LIBS CUDA::nvToolsExt)
      set(PROFILER_HAS_NVTX 1)
    elseif(TARGET CUDA::nvtx3)
      list(APPEND PROFILER_DEPENDENCY_LIBS CUDA::nvtx3)
      list(APPEND PROFILER_DEPENDENCY_COMPILE_DEFINITIONS PROFILER_CUDA_USE_NVTX3=1)
      set(PROFILER_HAS_NVTX 1)
    elseif(DEFINED CUDAToolkit_INCLUDE_DIR AND EXISTS "${CUDAToolkit_INCLUDE_DIR}/nvtx3/nvToolsExt.h")
      list(APPEND PROFILER_DEPENDENCY_INCLUDE_DIRS "${CUDAToolkit_INCLUDE_DIR}")
      list(APPEND PROFILER_DEPENDENCY_COMPILE_DEFINITIONS PROFILER_CUDA_USE_NVTX3=1)
      set(PROFILER_HAS_NVTX 1)
    endif()
  endif()
  if(PROFILER_REQUIRE_CUDA AND NOT PROFILER_HAS_CUDA)
    message(FATAL_ERROR "PROFILER_GPU_BACKEND=cuda but CUDA Toolkit was not found")
  endif()
  if(PROFILER_REQUIRE_NVTX AND NOT PROFILER_HAS_NVTX)
    message(FATAL_ERROR "NVTX was required but CUDA::nvToolsExt / CUDA::nvtx3 was not found")
  endif()
endif()

if(PROFILER_GPU_BACKEND STREQUAL "hip")
  find_package(hip QUIET)
  if(hip_FOUND)
    set(PROFILER_HAS_HIP 1)
    list(APPEND PROFILER_DEPENDENCY_LIBS hip::host)
  endif()
endif()

list(APPEND PROFILER_DEPENDENCY_COMPILE_DEFINITIONS
     PROFILER_HAS_CUDA=${PROFILER_HAS_CUDA} PROFILER_HAS_NVTX=${PROFILER_HAS_NVTX}
     PROFILER_HAS_HIP=${PROFILER_HAS_HIP}
)

if(PROFILER_ENABLE_KINETO)
  set(LIBKINETO_NOAIUPTI ON CACHE STRING "" FORCE)
  if(PROFILER_GPU_BACKEND STREQUAL "cuda")
    set(KINETO_BACKEND "cuda" CACHE STRING "libkineto GPU backend" FORCE)
    set(LIBKINETO_NOCUPTI OFF CACHE STRING "" FORCE)
  elseif(PROFILER_GPU_BACKEND STREQUAL "hip")
    set(KINETO_BACKEND "rocm" CACHE STRING "libkineto GPU backend" FORCE)
    set(LIBKINETO_NOCUPTI ON CACHE STRING "" FORCE)
  else()
    set(KINETO_BACKEND "cpu" CACHE STRING "libkineto GPU backend" FORCE)
    set(LIBKINETO_NOCUPTI ON CACHE STRING "" FORCE)
  endif()
  if(NOT PROFILER_HAS_HIP)
    set(LIBKINETO_NOROCTRACER ON CACHE STRING "" FORCE)
  endif()

  profiler_setup_fmt()
  if(COMMAND add_third_party_library)
    _profiler_third_party_root(_tpdir)
    set(FMT_SOURCE_DIR "${_tpdir}/fmt" CACHE STRING "Use host fmt" FORCE)
    add_third_party_library(
      kineto
      CONDITION
      PROFILER_ENABLE_KINETO
      LIBRARY_TYPE
      STATIC
      SOURCE_DIR
      "${_tpdir}/kineto/libkineto"
      DISABLE_WARNINGS
      FILTER_FLAGS
      "-flto=thin"
      "-flto=full"
      XSIGMA_TARGETS
      "Kineto::kineto=kineto"
      INCLUDE_DIR
      "${_tpdir}/kineto/libkineto/include"
      AUTO_ADD_TO_DEPENDENCIES
      OFF
    )
    if(_tpdir)
      list(APPEND PROFILER_DEPENDENCY_INCLUDE_DIRS
           "${_tpdir}/kineto/libkineto/include" "${_tpdir}/kineto/libkineto/src"
           "${_tpdir}/fmt/include"
      )
    endif()
    if(NOT TARGET Kineto::kineto)
      message(STATUS "Kineto not found — disabling PROFILER_ENABLE_KINETO")
      set(PROFILER_ENABLE_KINETO OFF)
    endif()
  else()
    profiler_setup_kineto()
    _profiler_third_party_root(_tpdir)
    if(_tpdir)
      list(APPEND PROFILER_DEPENDENCY_INCLUDE_DIRS
           "${_tpdir}/kineto/libkineto/include" "${_tpdir}/kineto/libkineto/src"
           "${_tpdir}/fmt/include"
      )
    endif()
    if(NOT TARGET Kineto::kineto)
      message(STATUS "Kineto not found — disabling PROFILER_ENABLE_KINETO")
      set(PROFILER_ENABLE_KINETO OFF)
    endif()
  endif()
endif()

if(PROFILER_ENABLE_ITT)
  if(COMMAND find_package)
    find_package(ITT QUIET)
  endif()
  if(NOT TARGET Itt::itt)
    profiler_setup_itt()
  endif()
  if(TARGET Itt::itt)
    list(APPEND PROFILER_DEPENDENCY_LIBS Itt::itt)
    _profiler_third_party_root(_itt_tp)
    if(_itt_tp AND EXISTS "${_itt_tp}/ittapi/include")
      list(APPEND PROFILER_DEPENDENCY_INCLUDE_DIRS "${_itt_tp}/ittapi/include")
    endif()
  else()
    message(STATUS "ITT API not found — disabling PROFILER_ENABLE_ITT")
    set(PROFILER_ENABLE_ITT OFF)
  endif()
  profiler_setup_fmt()
endif()

if(PROFILER_ENABLE_KINETO)
  list(APPEND PROFILER_DEPENDENCY_COMPILE_DEFINITIONS PROFILER_HAS_KINETO=1 PROFILER_HAS_ITT=0)
elseif(PROFILER_ENABLE_ITT)
  list(APPEND PROFILER_DEPENDENCY_COMPILE_DEFINITIONS PROFILER_HAS_KINETO=0 PROFILER_HAS_ITT=1)
else()
  list(APPEND PROFILER_DEPENDENCY_COMPILE_DEFINITIONS PROFILER_HAS_KINETO=0 PROFILER_HAS_ITT=0)
endif()

if(TARGET Fmt::fmt)
  list(APPEND PROFILER_DEPENDENCY_LIBS Fmt::fmt)
elseif(TARGET fmt::fmt)
  list(APPEND PROFILER_DEPENDENCY_LIBS fmt::fmt)
elseif(TARGET fmt)
  list(APPEND PROFILER_DEPENDENCY_LIBS fmt)
endif()
if(PROFILER_ENABLE_KINETO AND TARGET Kineto::kineto)
  list(APPEND PROFILER_DEPENDENCY_LIBS Kineto::kineto)
endif()

set(profiler_headers "")
set(profiler_sources "")

file(GLOB common_util_headers LIST_DIRECTORIES false "${CMAKE_CURRENT_SOURCE_DIR}/common/*.h")
file(GLOB common_util_sources LIST_DIRECTORIES false "${CMAKE_CURRENT_SOURCE_DIR}/common/*.cpp")
list(APPEND profiler_headers ${common_util_headers})
list(APPEND profiler_sources ${common_util_sources})

file(GLOB_RECURSE native_headers LIST_DIRECTORIES false "${CMAKE_CURRENT_SOURCE_DIR}/native/*.h")
file(GLOB_RECURSE native_sources LIST_DIRECTORIES false "${CMAKE_CURRENT_SOURCE_DIR}/native/*.cpp")
list(APPEND profiler_headers ${native_headers})
list(APPEND profiler_sources ${native_sources})

if(PROFILER_HAS_METAL)
  enable_language(OBJCXX)
  set(CMAKE_OBJCXX_STANDARD ${PROFILER_CXX_STANDARD})
  set(CMAKE_OBJCXX_STANDARD_REQUIRED ON)
  find_library(PROFILER_METAL_FRAMEWORK Metal REQUIRED)
  find_library(PROFILER_FOUNDATION_FRAMEWORK Foundation REQUIRED)
  file(GLOB_RECURSE profiler_mm LIST_DIRECTORIES false "${CMAKE_CURRENT_SOURCE_DIR}/native/*.mm")
  list(APPEND profiler_sources ${profiler_mm})
  set_source_files_properties(
    ${profiler_mm} PROPERTIES COMPILE_OPTIONS "-fobjc-arc;-Wno-deprecated-declarations"
  )
  list(APPEND PROFILER_DEPENDENCY_LIBS ${PROFILER_METAL_FRAMEWORK} ${PROFILER_FOUNDATION_FRAMEWORK})
  file(GLOB_RECURSE profiler_kineto_mm LIST_DIRECTORIES false
       "${CMAKE_CURRENT_SOURCE_DIR}/bespoke/base/*.mm"
  )
  list(APPEND profiler_sources ${profiler_kineto_mm})
  set_source_files_properties(
    ${profiler_kineto_mm} PROPERTIES COMPILE_OPTIONS "-fobjc-arc;-Wno-deprecated-declarations"
  )
endif()

if(PROFILER_ENABLE_KINETO)
  file(
    GLOB_RECURSE kineto_headers LIST_DIRECTORIES false
    "${CMAKE_CURRENT_SOURCE_DIR}/bespoke/base/*.h" "${CMAKE_CURRENT_SOURCE_DIR}/bespoke/kineto/*.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/bespoke/common/*.h"
  )
  file(
    GLOB_RECURSE kineto_sources LIST_DIRECTORIES false
    "${CMAKE_CURRENT_SOURCE_DIR}/bespoke/base/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/bespoke/kineto/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/bespoke/common/*.cpp"
  )
  file(GLOB_RECURSE KINETO_UNWIND_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/bespoke/common/unwind/*.cpp")
  list(REMOVE_ITEM kineto_sources ${KINETO_UNWIND_SOURCES})
  list(APPEND profiler_headers ${kineto_headers})
  list(APPEND profiler_sources ${kineto_sources})
endif()

set(PROFILER_NEEDS_FMT_HEADER_ONLY OFF)
if(PROFILER_ENABLE_ITT)
  if(NOT PROFILER_ENABLE_KINETO)
    file(
      GLOB_RECURSE itt_base_headers LIST_DIRECTORIES false
      "${CMAKE_CURRENT_SOURCE_DIR}/bespoke/base/*.h"
      "${CMAKE_CURRENT_SOURCE_DIR}/bespoke/common/*.h"
      "${CMAKE_CURRENT_SOURCE_DIR}/bespoke/kineto/kineto_shim.h"
      "${CMAKE_CURRENT_SOURCE_DIR}/bespoke/kineto/profiler_kineto.h"
      "${CMAKE_CURRENT_SOURCE_DIR}/bespoke/kineto/kineto_client_interface.h"
    )
    file(
      GLOB_RECURSE itt_base_sources LIST_DIRECTORIES false
      "${CMAKE_CURRENT_SOURCE_DIR}/bespoke/base/*.cpp"
      "${CMAKE_CURRENT_SOURCE_DIR}/bespoke/common/*.cpp"
      "${CMAKE_CURRENT_SOURCE_DIR}/bespoke/kineto/kineto_shim.cpp"
      "${CMAKE_CURRENT_SOURCE_DIR}/bespoke/kineto/profiler_kineto.cpp"
      "${CMAKE_CURRENT_SOURCE_DIR}/bespoke/kineto/kineto_client_interface.cpp"
    )
    list(APPEND profiler_headers ${itt_base_headers})
    list(APPEND profiler_sources ${itt_base_sources})
    set(PROFILER_NEEDS_FMT_HEADER_ONLY ON)
  endif()
  file(GLOB_RECURSE itt_headers LIST_DIRECTORIES false
       "${CMAKE_CURRENT_SOURCE_DIR}/bespoke/itt/*.h"
  )
  file(GLOB_RECURSE itt_sources LIST_DIRECTORIES false
       "${CMAKE_CURRENT_SOURCE_DIR}/bespoke/itt/*.cpp"
  )
  list(APPEND profiler_headers ${itt_headers})
  list(APPEND profiler_sources ${itt_sources})
endif()

add_library(Profiler ${profiler_sources} ${profiler_headers})
add_library(Profiler::Profiler ALIAS Profiler)
add_library(XSigmaProfiler::Profiler ALIAS Profiler)

if(BUILD_SHARED_LIBS)
  target_compile_definitions(Profiler PUBLIC PROFILER_SHARED_DEFINE PRIVATE PROFILER_BUILDING_DLL)
else()
  target_compile_definitions(Profiler PUBLIC PROFILER_STATIC_DEFINE)
endif()

if(PROFILER_NEEDS_FMT_HEADER_ONLY)
  target_compile_definitions(Profiler PRIVATE FMT_HEADER_ONLY=1)
endif()

set_target_properties(
  Profiler PROPERTIES CXX_STANDARD ${PROFILER_CXX_STANDARD} CXX_STANDARD_REQUIRED ON CXX_EXTENSIONS
                                                                                     OFF
                      EXPORT_NAME Profiler
)

if(MSVC)
  target_compile_options(Profiler PRIVATE /WX)
endif()

target_include_directories(
  Profiler
  PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
         $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}>
         $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/Testing>
         $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}/Testing>
         $<INSTALL_INTERFACE:include>
)

if(PROFILER_STANDALONE)
  set(_profiler_dep_vis PRIVATE)
else()
  set(_profiler_dep_vis PUBLIC)
endif()
# Headers from fmt / Kineto / ITT are included by Profiler sources and by
# in-tree tests/examples. Keep the libraries PRIVATE in standalone builds so
# install consumers only link Profiler::Profiler.
if(PROFILER_DEPENDENCY_INCLUDE_DIRS)
  target_include_directories(Profiler PUBLIC ${PROFILER_DEPENDENCY_INCLUDE_DIRS})
endif()
if(PROFILER_DEPENDENCY_LIBS)
  target_link_libraries(Profiler ${_profiler_dep_vis} ${PROFILER_DEPENDENCY_LIBS})
endif()
if(PROFILER_DEPENDENCY_COMPILE_DEFINITIONS)
  target_compile_definitions(Profiler PUBLIC ${PROFILER_DEPENDENCY_COMPILE_DEFINITIONS})
endif()

if(PROFILER_ENABLE_KINETO AND TARGET kineto AND CMAKE_GENERATOR STREQUAL "Xcode")
  set(_kineto_xcode_dummy "${CMAKE_CURRENT_BINARY_DIR}/kineto_xcode_dummy.cpp")
  if(NOT EXISTS "${_kineto_xcode_dummy}")
    file(WRITE "${_kineto_xcode_dummy}" "/* Xcode object-only static lib placeholder. */\n")
  endif()
  target_sources(kineto PRIVATE "${_kineto_xcode_dummy}")
  add_dependencies(Profiler kineto)
endif()

if(WIN32 AND TARGET CUDA::cupti)
  get_target_property(_profiler_cupti_implib CUDA::cupti IMPORTED_LOCATION)
  if(_profiler_cupti_implib)
    get_filename_component(_profiler_cupti_dir "${_profiler_cupti_implib}" DIRECTORY)
    file(GLOB _profiler_cupti_dll "${_profiler_cupti_dir}/cupti64_*.dll")
    if(_profiler_cupti_dll)
      add_custom_command(
        TARGET Profiler
        POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${_profiler_cupti_dll}"
                "$<TARGET_FILE_DIR:Profiler>"
        VERBATIM
      )
    endif()
  endif()
endif()

configure_file(
  "${CMAKE_CURRENT_SOURCE_DIR}/Testing/ProfilerTest.h.in"
  "${CMAKE_CURRENT_BINARY_DIR}/Testing/ProfilerTest.h" @ONLY
)

if(PROFILER_ENABLE_EXAMPLES)
  add_subdirectory(examples)
endif()

if(PROFILER_ENABLE_TESTING)
  add_subdirectory(Testing/Cxx)
endif()

if(PROFILER_ENABLE_INSTALL)
  install(
    TARGETS Profiler
    LIBRARY DESTINATION lib
    ARCHIVE DESTINATION lib
    RUNTIME DESTINATION bin
  )
  install(DIRECTORY common native bespoke util DESTINATION include FILES_MATCHING PATTERN "*.h")
  install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/profiler.h" DESTINATION include)
  file(
    GENERATE
    OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/ProfilerConfig.cmake"
    CONTENT
      "get_filename_component(PACKAGE_PREFIX_DIR \"\${CMAKE_CURRENT_LIST_DIR}/../../..\" ABSOLUTE)
if(NOT TARGET Profiler::Profiler)
  add_library(Profiler::Profiler SHARED IMPORTED)
  if(WIN32)
    set_target_properties(Profiler::Profiler PROPERTIES
      IMPORTED_LOCATION \"\${PACKAGE_PREFIX_DIR}/bin/$<TARGET_FILE_NAME:Profiler>\"
      IMPORTED_IMPLIB \"\${PACKAGE_PREFIX_DIR}/lib/$<TARGET_LINKER_FILE_NAME:Profiler>\"
      INTERFACE_INCLUDE_DIRECTORIES \"\${PACKAGE_PREFIX_DIR}/include\")
  else()
    set_target_properties(Profiler::Profiler PROPERTIES
      IMPORTED_LOCATION \"\${PACKAGE_PREFIX_DIR}/lib/$<TARGET_FILE_NAME:Profiler>\"
      INTERFACE_INCLUDE_DIRECTORIES \"\${PACKAGE_PREFIX_DIR}/include\")
  endif()
endif()
if(NOT TARGET XSigmaProfiler::Profiler)
  add_library(XSigmaProfiler::Profiler ALIAS Profiler::Profiler)
endif()
"
  )
  file(
    GENERATE
    OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/XSigmaProfilerConfig.cmake"
    CONTENT
      "include(\"\${CMAKE_CURRENT_LIST_DIR}/../Profiler/ProfilerConfig.cmake\")\n"
  )
  install(FILES "${CMAKE_CURRENT_BINARY_DIR}/ProfilerConfig.cmake"
          DESTINATION lib/cmake/Profiler
  )
  install(FILES "${CMAKE_CURRENT_BINARY_DIR}/XSigmaProfilerConfig.cmake"
          DESTINATION lib/cmake/XSigmaProfiler
  )
endif()

message(STATUS "Profiler: backend=${PROFILER_BACKEND} gpu=${PROFILER_GPU_BACKEND} "
               "cuda=${PROFILER_HAS_CUDA} nvtx=${PROFILER_HAS_NVTX} "
               "standalone=${PROFILER_STANDALONE}"
)
