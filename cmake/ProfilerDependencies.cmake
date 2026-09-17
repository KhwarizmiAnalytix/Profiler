# Resolve fmt, Kineto, ITT, and GTest without requiring any particular host build.

set(_PROFILER_CMAKE_DIR "${CMAKE_CURRENT_LIST_DIR}")

function(_profiler_third_party_root out_var)
  if(PROFILER_THIRD_PARTY_DIR)
    set(${out_var} "${PROFILER_THIRD_PARTY_DIR}" PARENT_SCOPE)
    return()
  endif()
  # Anchor on _PROFILER_CMAKE_DIR (this file's own directory), not
  # CMAKE_CURRENT_SOURCE_DIR: callers like Testing/Cxx/CMakeLists.txt run
  # with CMAKE_CURRENT_SOURCE_DIR pointing at their own subdirectory, not
  # the repo root, which would make third_party/ resolve to the wrong path.
  get_filename_component(_profiler_repo_root "${_PROFILER_CMAKE_DIR}/.." ABSOLUTE)
  if(EXISTS "${_profiler_repo_root}/third_party/fmt/CMakeLists.txt")
    set(${out_var} "${_profiler_repo_root}/third_party" PARENT_SCOPE)
  else()
    set(${out_var} "" PARENT_SCOPE)
  endif()
endfunction()

function(profiler_setup_fmt)
  if(TARGET Fmt::fmt OR TARGET fmt OR TARGET fmt::fmt)
    if(NOT TARGET fmt::fmt-header-only AND TARGET fmt)
      add_library(fmt::fmt-header-only ALIAS fmt)
    endif()
    return()
  endif()
  _profiler_third_party_root(_tp)
  if(NOT _tp OR NOT EXISTS "${_tp}/fmt/CMakeLists.txt")
    message(FATAL_ERROR
            "fmt is required and must be vendored under third_party/fmt. "
            "Initialize it with git submodule update --init --recursive.")
  endif()
  set(FMT_TEST OFF CACHE BOOL "" FORCE)
  set(FMT_DOC OFF CACHE BOOL "" FORCE)
  set(_profiler_fmt_shared "${BUILD_SHARED_LIBS}")
  set(BUILD_SHARED_LIBS OFF)
  add_subdirectory("${_tp}/fmt" "${CMAKE_BINARY_DIR}/_profiler_fmt" EXCLUDE_FROM_ALL)
  set(BUILD_SHARED_LIBS "${_profiler_fmt_shared}")
  if(TARGET fmt)
    target_compile_definitions(fmt PUBLIC FMT_USE_CONSTEVAL=0)
    set_property(TARGET fmt PROPERTY POSITION_INDEPENDENT_CODE ON)
  endif()
  if(NOT TARGET fmt::fmt-header-only AND TARGET fmt)
    add_library(fmt::fmt-header-only ALIAS fmt)
  endif()
endfunction()

function(profiler_setup_kineto)
  if(TARGET Kineto::kineto OR TARGET kineto)
    if(NOT TARGET Kineto::kineto AND TARGET kineto)
      add_library(Kineto::kineto ALIAS kineto)
    endif()
    return()
  endif()
  _profiler_third_party_root(_tp)
  if(NOT _tp OR NOT EXISTS "${_tp}/kineto/libkineto/CMakeLists.txt")
    message(FATAL_ERROR
            "Kineto is required for PROFILER_BACKEND=KINETO and must be vendored under "
            "third_party/kineto. Initialize it with git submodule update --init --recursive.")
  endif()
  set(_kineto_src "${_tp}/kineto/libkineto")
  if(EXISTS "${_tp}/fmt")
    set(FMT_SOURCE_DIR "${_tp}/fmt" CACHE STRING "" FORCE)
  endif()

  set(KINETO_BUILD_TESTS OFF CACHE BOOL "" FORCE)
  set(KINETO_LIBRARY_TYPE "static" CACHE STRING "" FORCE)
  add_subdirectory("${_kineto_src}" "${CMAKE_BINARY_DIR}/_profiler_kineto" EXCLUDE_FROM_ALL)
  # Kineto defaults to header-only fmt, while Profiler links compiled fmt.
  # Mixing them produces duplicate fmt symbols under MSVC. Use the same
  # compiled target for Kineto's object libraries and their final archive.
  if(TARGET Fmt::fmt)
    set(_kineto_fmt_target Fmt::fmt)
  elseif(TARGET fmt::fmt)
    set(_kineto_fmt_target fmt::fmt)
  else()
    set(_kineto_fmt_target fmt)
  endif()
  foreach(_kineto_object_target kineto_base kineto_api)
    if(TARGET ${_kineto_object_target})
      get_target_property(_kineto_links ${_kineto_object_target} LINK_LIBRARIES)
      string(REPLACE "fmt::fmt-header-only" "${_kineto_fmt_target}"
             _kineto_links "${_kineto_links}")
      set_property(TARGET ${_kineto_object_target} PROPERTY LINK_LIBRARIES "${_kineto_links}")
    endif()
  endforeach()
  # $<TARGET_OBJECTS:...> does not propagate the object libraries' link dependencies.
  # Append properties directly: upstream uses different target_link_libraries
  # signatures for CUDA and ROCm, which cannot be mixed with another signature.
  set_property(TARGET kineto APPEND PROPERTY LINK_LIBRARIES
               $<BUILD_INTERFACE:${_kineto_fmt_target}>)
  set_property(TARGET kineto APPEND PROPERTY INTERFACE_LINK_LIBRARIES
               $<LINK_ONLY:$<BUILD_INTERFACE:${_kineto_fmt_target}>>)
  set(_fmt_kineto_compat "${_PROFILER_CMAKE_DIR}/fmt_kineto_compat.h")
  foreach(_kineto_target kineto kineto_base kineto_api)
    if(TARGET ${_kineto_target})
      set_property(TARGET ${_kineto_target} PROPERTY POSITION_INDEPENDENT_CODE ON)
      target_compile_definitions(${_kineto_target} PRIVATE FMT_USE_CONSTEVAL=0)
      if(EXISTS "${_fmt_kineto_compat}")
        if(MSVC)
          target_compile_options(${_kineto_target} PRIVATE "/FI${_fmt_kineto_compat}")
        else()
          target_compile_options(${_kineto_target} PRIVATE "-include" "${_fmt_kineto_compat}")
        endif()
      endif()
    endif()
  endforeach()
  if(TARGET kineto_base AND CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    if(MSVC)
      foreach(_kt kineto_base kineto_api)
        if(TARGET ${_kt})
          target_compile_options(${_kt} PRIVATE /clang:-O2)
        endif()
      endforeach()
    else()
      target_compile_options(kineto_base PRIVATE -O2)
    endif()
  endif()
  if(TARGET kineto)
    # libkineto puts public headers on kineto_base/kineto_api, not kineto.
    # Consumers that link kineto still need libkineto/include.
    target_include_directories(
      kineto PUBLIC $<BUILD_INTERFACE:${_kineto_src}/include>
                    $<BUILD_INTERFACE:${_kineto_src}/src>
    )
    if(NOT TARGET Kineto::kineto)
      add_library(Kineto::kineto ALIAS kineto)
    endif()
  endif()
endfunction()

function(profiler_setup_itt)
  if(TARGET Itt::itt)
    return()
  endif()
  _profiler_third_party_root(_tp)
  if(NOT _tp OR NOT EXISTS "${_tp}/ittapi/CMakeLists.txt")
    message(FATAL_ERROR
            "ittapi is required for PROFILER_BACKEND=ITT and must be vendored under "
            "third_party/ittapi. Initialize it with git submodule update --init --recursive.")
  endif()
  add_subdirectory("${_tp}/ittapi" "${CMAKE_BINARY_DIR}/_profiler_itt" EXCLUDE_FROM_ALL)
  if(TARGET ittnotify)
    set_property(TARGET ittnotify PROPERTY POSITION_INDEPENDENT_CODE ON)
    if(NOT TARGET Itt::itt)
      add_library(Itt::itt ALIAS ittnotify)
    endif()
  endif()
endfunction()

function(profiler_setup_gtest)
  if(TARGET GTest::gtest_main OR TARGET gtest_main)
    if(TARGET gtest AND NOT TARGET GTest::gtest)
      add_library(GTest::gtest ALIAS gtest)
    endif()
    if(TARGET gtest_main AND NOT TARGET GTest::gtest_main)
      add_library(GTest::gtest_main ALIAS gtest_main)
    endif()
    return()
  endif()
  if(COMMAND profiler_host_add_googletest)
    profiler_host_add_googletest()
    return()
  endif()
  _profiler_third_party_root(_tp)
  if(NOT _tp OR NOT EXISTS "${_tp}/googletest/CMakeLists.txt")
    message(FATAL_ERROR
            "GoogleTest is required for PROFILER_ENABLE_TESTING and must be vendored under "
            "third_party/googletest. Initialize it with git submodule update --init --recursive.")
  endif()
  # GoogleTest/GoogleMock don't mark every internal cross-translation-unit
  # symbol with GTEST_API_ (e.g. testing::internal::g_gmock_mutex), so
  # building them as shared libraries drops symbols a DLL boundary would
  # need and gmock_main fails to link against gmock with an undefined
  # symbol. Force a static build here regardless of the project's
  # BUILD_SHARED_LIBS, the same way profiler_setup_fmt() does, while still
  # matching Profiler's runtime library (gtest_force_shared_crt) so the test
  # binary and gtest/gmock agree on /MD vs /MT.
  set(_profiler_gtest_shared "${BUILD_SHARED_LIBS}")
  set(BUILD_SHARED_LIBS OFF)
  set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
  set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
  add_subdirectory("${_tp}/googletest" "${CMAKE_BINARY_DIR}/_profiler_gtest"
                   EXCLUDE_FROM_ALL)
  set(BUILD_SHARED_LIBS "${_profiler_gtest_shared}")
  if(TARGET gtest AND NOT TARGET GTest::gtest)
    add_library(GTest::gtest ALIAS gtest)
  endif()
  if(TARGET gtest_main AND NOT TARGET GTest::gtest_main)
    add_library(GTest::gtest_main ALIAS gtest_main)
  endif()
endfunction()

function(profiler_setup_benchmark)
  if(TARGET benchmark::benchmark OR TARGET benchmark)
    if(TARGET benchmark AND NOT TARGET benchmark::benchmark)
      add_library(benchmark::benchmark ALIAS benchmark)
    endif()
    return()
  endif()
  _profiler_third_party_root(_tp)
  if(NOT _tp OR NOT EXISTS "${_tp}/benchmark/CMakeLists.txt")
    message(FATAL_ERROR
            "Google Benchmark is required for PROFILER_ENABLE_BENCHMARKS. "
            "Initialize third_party/benchmark with git submodule update --init --recursive.")
  endif()
  set(BENCHMARK_ENABLE_GTEST_TESTS OFF CACHE BOOL "" FORCE)
  set(BENCHMARK_ENABLE_TESTING OFF CACHE BOOL "" FORCE)
  set(BENCHMARK_ENABLE_INSTALL OFF CACHE BOOL "" FORCE)
  set(BENCHMARK_DOWNLOAD_DEPENDENCIES OFF CACHE BOOL "" FORCE)
  add_subdirectory("${_tp}/benchmark" "${CMAKE_BINARY_DIR}/_profiler_benchmark" EXCLUDE_FROM_ALL)
  if(TARGET benchmark AND NOT TARGET benchmark::benchmark)
    add_library(benchmark::benchmark ALIAS benchmark)
  endif()
endfunction()
