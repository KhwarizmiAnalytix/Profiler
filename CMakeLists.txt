# Profiler — standalone CMake package. Any C++ project can consume it, either
# via find_package(Profiler) or by embedding this tree as a subdirectory.
cmake_minimum_required(VERSION 3.22)

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    project(
        Profiler
        VERSION 1.0.1
        DESCRIPTION "Standalone C++ CPU/GPU profiler"
        HOMEPAGE_URL "https://github.com/KhwarizmiAnalytix/Profiler"
        LANGUAGES CXX C
    )
    set(PROFILER_STANDALONE ON)
    include(CTest)
    enable_testing()
    set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
    set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib")
    set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib")
else()
    set(PROFILER_STANDALONE OFF)
endif()

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(ProfilerOptions)
include(ProfilerDependencies)
include(ProfilerCoverage)
include(ProfilerSanitizers)
include(ProfilerLinking)

set(CMAKE_CXX_STANDARD ${PROFILER_CXX_STANDARD})
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(PROFILER_BACKEND STREQUAL "KINETO")
    set(PROFILER_ENABLE_KINETO ON)
    set(PROFILER_ENABLE_ITT OFF)
elseif(PROFILER_BACKEND STREQUAL "ITT")
    set(PROFILER_ENABLE_KINETO OFF)
    set(PROFILER_ENABLE_ITT ON)
else()
    # NONE: native CPU collection only, no instrumentation backend compiled in
    # at all (design-review.md section 7's "allow a native-only build").
    set(PROFILER_ENABLE_KINETO OFF)
    set(PROFILER_ENABLE_ITT OFF)
endif()
set(PROFILER_ENABLE_NATIVE_PROFILER ON CACHE BOOL "Native pipeline is always compiled" FORCE)

set(PROFILER_DEPENDENCY_LIBS "")
set(PROFILER_DEPENDENCY_INCLUDE_DIRS "")
set(PROFILER_DEPENDENCY_COMPILE_DEFINITIONS "")

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
        elseif(DEFINED CUDAToolkit_INCLUDE_DIR AND EXISTS
                                                   "${CUDAToolkit_INCLUDE_DIR}/nvtx3/nvToolsExt.h"
        )
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
    if(PROFILER_REQUIRE_HIP AND NOT PROFILER_HAS_HIP)
        message(FATAL_ERROR "PROFILER_GPU_BACKEND=hip but the HIP/ROCm toolkit was not found")
    endif()
endif()

list(APPEND PROFILER_DEPENDENCY_COMPILE_DEFINITIONS PROFILER_HAS_CUDA=${PROFILER_HAS_CUDA}
     PROFILER_HAS_NVTX=${PROFILER_HAS_NVTX} PROFILER_HAS_HIP=${PROFILER_HAS_HIP}
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
    profiler_setup_kineto()
    _profiler_third_party_root(_tpdir)
    if(_tpdir)
        list(APPEND PROFILER_DEPENDENCY_INCLUDE_DIRS "${_tpdir}/kineto/libkineto/include"
             "${_tpdir}/kineto/libkineto/src" "${_tpdir}/fmt/include"
        )
    endif()
    if(NOT TARGET Kineto::kineto)
        message(STATUS "Kineto not found — disabling PROFILER_ENABLE_KINETO")
        set(PROFILER_ENABLE_KINETO OFF)
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

set(PROFILER_SRC_DIR "${CMAKE_CURRENT_SOURCE_DIR}/Profiler")

set(profiler_headers "${PROFILER_SRC_DIR}/profiler.h")
set(profiler_sources "")

file(GLOB common_util_headers LIST_DIRECTORIES false "${PROFILER_SRC_DIR}/common/*.h")
file(GLOB common_util_sources LIST_DIRECTORIES false "${PROFILER_SRC_DIR}/common/*.cpp")
file(GLOB util_headers LIST_DIRECTORIES false "${PROFILER_SRC_DIR}/util/*.h")
list(APPEND profiler_headers ${common_util_headers} ${util_headers})
list(APPEND profiler_sources ${common_util_sources})

file(GLOB_RECURSE native_headers LIST_DIRECTORIES false "${PROFILER_SRC_DIR}/native/*.h")
file(GLOB_RECURSE native_sources LIST_DIRECTORIES false "${PROFILER_SRC_DIR}/native/*.cpp")
list(APPEND profiler_headers ${native_headers})
list(APPEND profiler_sources ${native_sources})

if(PROFILER_ENABLE_KINETO)
    file(GLOB_RECURSE kineto_headers LIST_DIRECTORIES false
         "${PROFILER_SRC_DIR}/bespoke/base/*.h" "${PROFILER_SRC_DIR}/bespoke/kineto/*.h"
         "${PROFILER_SRC_DIR}/bespoke/common/*.h"
    )
    file(GLOB_RECURSE kineto_sources LIST_DIRECTORIES false
         "${PROFILER_SRC_DIR}/bespoke/base/*.cpp" "${PROFILER_SRC_DIR}/bespoke/kineto/*.cpp"
         "${PROFILER_SRC_DIR}/bespoke/common/*.cpp"
    )
    file(GLOB_RECURSE KINETO_UNWIND_SOURCES "${PROFILER_SRC_DIR}/bespoke/common/unwind/*.cpp")
    list(REMOVE_ITEM kineto_sources ${KINETO_UNWIND_SOURCES})
    list(APPEND profiler_headers ${kineto_headers})
    list(APPEND profiler_sources ${kineto_sources})
endif()

set(PROFILER_NEEDS_FMT_HEADER_ONLY OFF)
if(PROFILER_ENABLE_ITT)
    if(NOT PROFILER_ENABLE_KINETO)
        file(
            GLOB_RECURSE itt_base_headers
            LIST_DIRECTORIES false
            "${PROFILER_SRC_DIR}/bespoke/base/*.h"
            "${PROFILER_SRC_DIR}/bespoke/common/*.h"
            "${PROFILER_SRC_DIR}/bespoke/kineto/kineto_shim.h"
            "${PROFILER_SRC_DIR}/bespoke/kineto/profiler_kineto.h"
            "${PROFILER_SRC_DIR}/bespoke/kineto/kineto_client_interface.h"
        )
        file(
            GLOB_RECURSE itt_base_sources
            LIST_DIRECTORIES false
            "${PROFILER_SRC_DIR}/bespoke/base/*.cpp"
            "${PROFILER_SRC_DIR}/bespoke/common/*.cpp"
            "${PROFILER_SRC_DIR}/bespoke/kineto/kineto_shim.cpp"
            "${PROFILER_SRC_DIR}/bespoke/kineto/profiler_kineto.cpp"
            "${PROFILER_SRC_DIR}/bespoke/kineto/kineto_client_interface.cpp"
        )
        list(APPEND profiler_headers ${itt_base_headers})
        list(APPEND profiler_sources ${itt_base_sources})
        set(PROFILER_NEEDS_FMT_HEADER_ONLY ON)
    endif()
    file(GLOB_RECURSE itt_headers LIST_DIRECTORIES false "${PROFILER_SRC_DIR}/bespoke/itt/*.h")
    file(GLOB_RECURSE itt_sources LIST_DIRECTORIES false "${PROFILER_SRC_DIR}/bespoke/itt/*.cpp")
    list(APPEND profiler_headers ${itt_headers})
    list(APPEND profiler_sources ${itt_sources})
endif()

add_library(Profiler ${profiler_sources} ${profiler_headers})
add_library(Profiler::Profiler ALIAS Profiler)

if(BUILD_SHARED_LIBS)
    target_compile_definitions(Profiler PUBLIC PROFILER_SHARED_DEFINE PRIVATE PROFILER_BUILDING_DLL)
else()
    target_compile_definitions(Profiler PUBLIC PROFILER_STATIC_DEFINE)
endif()

if(PROFILER_NEEDS_FMT_HEADER_ONLY)
    target_compile_definitions(Profiler PRIVATE FMT_HEADER_ONLY=1)
endif()

if(PROFILER_ENABLE_COVERAGE)
    profiler_enable_coverage(Profiler)
endif()
profiler_enable_sanitizer(Profiler)
profiler_keep_static_registrations(Profiler)

set_target_properties(
    Profiler
    PROPERTIES CXX_STANDARD ${PROFILER_CXX_STANDARD}
               CXX_STANDARD_REQUIRED ON
               CXX_EXTENSIONS OFF
               EXPORT_NAME Profiler
)

if(MSVC)
    # C4392 is a false positive inside MSVC's own zmmintrin.h (AVX-512 FP16
    # intrinsic declarations disagree with their own arg count) on newer
    # toolsets (reproduced with VS 2026's 14.51.36231); it fires from the
    # system header itself, not from anything Profiler controls, so /WX
    # would otherwise fail the build on a toolset bug rather than our code.
    target_compile_options(Profiler PRIVATE /WX /wd4392)
endif()

target_include_directories(
    Profiler
    PUBLIC $<BUILD_INTERFACE:${PROFILER_SRC_DIR}> $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}>
           $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/Testing>
           $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}/Testing> $<INSTALL_INTERFACE:include>
)

if(PROFILER_STANDALONE)
    set(_profiler_dep_vis PRIVATE)
else()
    set(_profiler_dep_vis PUBLIC)
endif()
# Headers from fmt / Kineto / ITT are included by Profiler sources and by
# in-tree tests/examples. Keep the libraries PRIVATE in standalone builds so
# install consumers only link Profiler::Profiler. Wrapped in
# $<BUILD_INTERFACE:> so these source-tree paths and vendored targets still
# propagate to in-tree consumers (tests/examples/benchmarks) but are excluded
# from install(EXPORT)'s INSTALL_INTERFACE -- without this, install(EXPORT)
# refuses to export Profiler at all (a source-directory path can't appear in
# an installed package's INTERFACE_INCLUDE_DIRECTORIES; a STATIC library's
# PRIVATE link libraries are auto-propagated to consumers via
# $<LINK_ONLY:...> since the archive alone can't resolve its own symbols --
# see 5.D's STATIC third-party archive install block below, which supplies
# the same dependencies for a STATIC install consumer through an explicit,
# relocatable path instead).
if(PROFILER_DEPENDENCY_INCLUDE_DIRS)
    set(_profiler_dep_include_dirs_build_only "")
    foreach(_profiler_dep_dir IN LISTS PROFILER_DEPENDENCY_INCLUDE_DIRS)
        list(APPEND _profiler_dep_include_dirs_build_only "$<BUILD_INTERFACE:${_profiler_dep_dir}>")
    endforeach()
    target_include_directories(Profiler PUBLIC ${_profiler_dep_include_dirs_build_only})
    unset(_profiler_dep_include_dirs_build_only)
endif()
if(PROFILER_DEPENDENCY_LIBS)
    set(_profiler_dep_libs_build_only "")
    foreach(_profiler_dep_lib IN LISTS PROFILER_DEPENDENCY_LIBS)
        list(APPEND _profiler_dep_libs_build_only "$<BUILD_INTERFACE:${_profiler_dep_lib}>")
    endforeach()
    target_link_libraries(Profiler ${_profiler_dep_vis} ${_profiler_dep_libs_build_only})
    unset(_profiler_dep_libs_build_only)
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

if(PROFILER_ENABLE_BENCHMARKS)
    add_subdirectory(benchmarks)
endif()

if(PROFILER_ENABLE_TESTING)
    add_subdirectory(Testing/Cxx)
endif()

if(PROFILER_ENABLE_INSTALL)
    install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/LICENSE" "${CMAKE_CURRENT_SOURCE_DIR}/NOTICE"
            DESTINATION share/licenses/Profiler
    )
    # Phase 5 (design-review.md section 7 / finding 13): install only the
    # public surface -- profiler.h plus every header its own #include chain
    # transitively needs to resolve for an external consumer, not every
    # header under Profiler/ (which used to include native/cpu, native/gpu,
    # native/platform, bespoke/base, bespoke/kineto, bespoke/itt, etc. --
    # implementation-detail headers profiler.h's own doc comment and
    # docs/profiler.md already describe as internal, but the build never
    # enforced). This list was derived mechanically, not guessed: it is the
    # exact transitive closure of profiler.h's quoted #include graph
    # (common/instrumentation.h's bespoke/common/record_function.h pull-in
    # included, since it's reachable under PROFILER_HAS_KINETO/_ITT). Keep it
    # in sync with that closure -- CI's static-link job's header-surface
    # check re-derives and verifies this.
    set(_profiler_public_headers
        profiler.h
        common/align_of.h
        common/annotation.h
        common/approximate_clock.h
        common/capture.h
        common/device.h
        common/flat_hash.h
        common/instrumentation.h
        common/no_init.h
        common/profiler_export.h
        common/profiler_macros.h
        common/profiler_strings.h
        common/session.h
        common/small_vector.h
        native/analysis/hotspot_report.h
        native/analysis/statistical_analyzer.h
        native/core/profiler_interface.h
        native/core/profiler_lock.h
        native/core/profiler_options.h
        native/core/profiler_status.h
        native/exporters/xplane/xplane.h
        native/memory/memory_tracker.h
        native/memory/scoped_memory_debug_annotation.h
        native/session/profiler.h
        native/session/profiler_report.h
        native/tracing/traceme.h
        native/tracing/traceme_encode.h
        native/tracing/traceme_recorder.h
        bespoke/common/record_function.h
    )
    foreach(_profiler_public_header ${_profiler_public_headers})
        get_filename_component(_profiler_public_header_dir "${_profiler_public_header}" DIRECTORY)
        install(FILES "${PROFILER_SRC_DIR}/${_profiler_public_header}"
                DESTINATION "include/${_profiler_public_header_dir}"
        )
    endforeach()
    unset(_profiler_public_header_dir)
    unset(_profiler_public_header)
    unset(_profiler_public_headers)
    # Real install(EXPORT) target-export machinery (Phase 5.D, design-review.md
    # section 7) instead of a hand-rolled file(GENERATE ...) string. This
    # natively handles the SHARED/STATIC imported-target kind and Windows
    # IMPORTED_IMPLIB/IMPORTED_LOCATION pairing -- both previously computed by
    # hand above -- because Profiler's usage requirements (include dirs via
    # $<BUILD_INTERFACE:>/$<INSTALL_INTERFACE:> above) are already expressed
    # as real target properties, which is exactly what install(EXPORT) needs.
    install(
        TARGETS Profiler
        EXPORT ProfilerTargets
        LIBRARY DESTINATION lib
        ARCHIVE DESTINATION lib
        RUNTIME DESTINATION bin
    )
    install(EXPORT ProfilerTargets NAMESPACE Profiler:: DESTINATION lib/cmake/Profiler)

    # Third-party archives Profiler links PRIVATE-ly (cmake/ProfilerDependencies.cmake):
    # deliberate for SHARED, where they're already linked into libProfiler.so and an
    # install consumer needs nothing else -- not true for STATIC, where a plain archive
    # resolves none of its own symbols. install(EXPORT) only exports usage requirements
    # for targets that are themselves part of an export set; fmt/kineto/ittnotify are
    # vendored via FetchContent with no installed find_package() identity of their own,
    # so they can't join ProfilerTargets' export set. Without separately installing and
    # referencing their archives, a find_package() consumer of a STATIC build gets
    # undefined-symbol errors for every libkineto/fmt/ITT symbol Profiler itself calls
    # (reproduced and confirmed fixed by this block, originally without CUDA/HIP).
    # CUDA::cudart/nvToolsExt/nvtx3 and hip::host are real, separately find_package()-able
    # targets (find_dependency() in ProfilerConfig.cmake.in re-creates them in the
    # consumer's own scope), so those are propagated by name instead of by installed path.
    if(NOT BUILD_SHARED_LIBS)
        set(_profiler_static_install_targets fmt)
        if(TARGET kineto)
            list(APPEND _profiler_static_install_targets kineto)
        endif()
        if(TARGET ittnotify)
            list(APPEND _profiler_static_install_targets ittnotify)
        endif()
        install(TARGETS ${_profiler_static_install_targets} ARCHIVE DESTINATION lib)
        # One pair of quotes around the whole semicolon-joined list (not one
        # pair per item) -- set_target_properties() takes a single value per
        # property, and a quoted "a;b;c" is the standard way to pass a list
        # as that one value.
        set(_profiler_static_link_libs "")
        foreach(_profiler_dep_tgt IN LISTS _profiler_static_install_targets)
            string(APPEND _profiler_static_link_libs
                   "\${PACKAGE_PREFIX_DIR}/lib/$<TARGET_FILE_NAME:${_profiler_dep_tgt}>;"
            )
        endforeach()
        foreach(_profiler_cuda_hip_lib IN LISTS PROFILER_DEPENDENCY_LIBS)
            if(_profiler_cuda_hip_lib MATCHES "^(CUDA|hip)::")
                string(APPEND _profiler_static_link_libs "${_profiler_cuda_hip_lib};")
            endif()
        endforeach()
        # kineto links CUPTI itself (its own CMakeLists' find_package(CUDAToolkit),
        # never added to PROFILER_DEPENDENCY_LIBS since Profiler never calls CUPTI
        # directly) -- since kineto is only installed as a raw archive above, not
        # a real exported target, that transitive requirement is otherwise
        # invisible here. Reproduced: a STATIC find_package() consumer with a
        # CUPTI-enabled kineto got 21 unresolved cuptiActivity*/cuptiSubscribe/...
        # externals without this. Same reasoning as cudart/nvtx3 above -- a real,
        # separately find_package()-able target the consumer's own
        # find_dependency(CUDAToolkit) call already re-creates.
        if(TARGET CUDA::cupti)
            string(APPEND _profiler_static_link_libs "CUDA::cupti;")
        endif()
        # PROFILER_DEPENDENCY_INCLUDE_DIRS also holds kineto/fmt/ITT build-tree
        # include paths (needed only to build Profiler itself, already
        # $<BUILD_INTERFACE:>-scoped on the Profiler target above) -- none of
        # the curated public headers need them, so only the one CUDA-specific
        # entry (CUDAToolkit_INCLUDE_DIR's header-only nvtx3 fallback, when no
        # CUDA::nvtx3 target is available) is deliberately re-propagated here,
        # by variable name rather than this build's absolute path, so it
        # re-resolves in the consumer's own scope (set there by
        # ProfilerConfig.cmake.in's find_dependency(CUDAToolkit)) and survives
        # relocation. Everything else in the list is intentionally dropped.
        set(_profiler_static_third_party_includes "")
        if(DEFINED CUDAToolkit_INCLUDE_DIR AND CUDAToolkit_INCLUDE_DIR IN_LIST
                                               PROFILER_DEPENDENCY_INCLUDE_DIRS
        )
            set(_profiler_static_third_party_includes "\${CUDAToolkit_INCLUDE_DIR};")
        endif()
        # $<TARGET_FILE_NAME:...> varies by config (fmt's DEBUG_POSTFIX, e.g.),
        # so under a multi-config generator (Visual Studio) this content
        # legitimately differs per configuration -- file(GENERATE) requires a
        # distinct OUTPUT per distinct CONTENT, hence the $<CONFIG> in the
        # generated (not installed) file name. install(FILES ... RENAME)
        # collapses it back to one fixed installed name: `cmake --install`
        # always targets exactly one config, so exactly one of these ever
        # actually gets installed under that fixed name.
        file(
            GENERATE
            OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/ProfilerStaticThirdParty-$<CONFIG>.cmake"
            CONTENT
                "set_property(TARGET Profiler::Profiler APPEND PROPERTY
    INTERFACE_LINK_LIBRARIES \"${_profiler_static_link_libs}\")
$<$<BOOL:${_profiler_static_third_party_includes}>:set_property(TARGET Profiler::Profiler APPEND PROPERTY
    INTERFACE_INCLUDE_DIRECTORIES \"${_profiler_static_third_party_includes}\")
>"
        )
        install(FILES "${CMAKE_CURRENT_BINARY_DIR}/ProfilerStaticThirdParty-$<CONFIG>.cmake"
                DESTINATION lib/cmake/Profiler RENAME ProfilerStaticThirdParty.cmake
        )
    endif()

    include(CMakePackageConfigHelpers)
    configure_package_config_file(
        "${CMAKE_CURRENT_SOURCE_DIR}/cmake/ProfilerConfig.cmake.in"
        "${CMAKE_CURRENT_BINARY_DIR}/ProfilerConfig.cmake" INSTALL_DESTINATION lib/cmake/Profiler
    )
    if(PROJECT_VERSION)
        write_basic_package_version_file(
            "${CMAKE_CURRENT_BINARY_DIR}/ProfilerConfigVersion.cmake" VERSION "${PROJECT_VERSION}"
            COMPATIBILITY SameMajorVersion
        )
        install(FILES "${CMAKE_CURRENT_BINARY_DIR}/ProfilerConfigVersion.cmake"
                DESTINATION lib/cmake/Profiler
        )
    endif()
    install(FILES "${CMAKE_CURRENT_BINARY_DIR}/ProfilerConfig.cmake" DESTINATION lib/cmake/Profiler)
    install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/cmake/ProfilerLinking.cmake"
            DESTINATION lib/cmake/Profiler
    )
endif()

message(STATUS "Profiler: backend=${PROFILER_BACKEND} gpu=${PROFILER_GPU_BACKEND} "
               "cuda=${PROFILER_HAS_CUDA} nvtx=${PROFILER_HAS_NVTX} "
               "standalone=${PROFILER_STANDALONE}"
)
