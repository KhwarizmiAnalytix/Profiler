set(TestFiles
    "${CMAKE_CURRENT_SOURCE_DIR}/TestCommonContainers.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/TestHotspotReport.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/TestNativeUtils.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/TestProfilerBackendCapabilities.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/TestProfilerBackendFunction.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/TestProfilerBackendGpuFallback.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/TestProfilerBackendMemory.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/TestProfilerBackendMetadata.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/TestProfilerBackendOutput.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/TestProfilerBackendPythonTracer.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/TestCrossBackendGoldenScenario.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/TestProfilerFailureInjection.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/TestProfilerLockAndEnv.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/TestPublicOptionsTripwire.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/TestProfilerChromeTraceHierarchical.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/TestProfilerHeavyFunction.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/TestProfilerGpuTracer.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/TestProfilerGpuRealHardware.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/TestProfilerLifecycleRegressions.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/TestProfilerNativeHotspot.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/TestProfilerScopeOverhead.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/TestProfilerSoak.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/TestStatisticalAnalysis.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/TestProfilerPublicApi.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/TestScopeTreeBuilder.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/TestProfilerThreadpoolTracing.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/TestProfilerXPlanePipeline.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/TestXPlaneBuilders.cpp"
)

profiler_setup_gtest()

add_executable(ProfilerCxxTests ${TestFiles})

target_compile_definitions(
  ProfilerCxxTests PRIVATE PROFILER_HAS_GTEST=1 PROFILER_GOOGLE_TEST USE_GTEST _VARIADIC_MAX=10
)

target_link_libraries(ProfilerCxxTests PRIVATE GTest::gtest_main Profiler::Profiler)

# The real-hardware CUDA tests launch a kernel, so this source needs NVCC
# rather than the host C++ compiler. CUDA::cudart also supplies cuda_runtime_api.h
# to the test target; Profiler keeps that dependency private in standalone builds.
if(PROFILER_HAS_CUDA)
  enable_language(CUDA)
  set_source_files_properties(TestProfilerGpuRealHardware.cpp PROPERTIES LANGUAGE CUDA)
  # GitHub-hosted runners install a CUDA Toolkit but do not expose a GPU, so
  # `native` cannot be used here. sm_75 is supported by the CUDA versions in
  # CI and emits compatible PTX for newer devices.
  set_target_properties(ProfilerCxxTests PROPERTIES CUDA_ARCHITECTURES 75)
  target_link_libraries(ProfilerCxxTests PRIVATE CUDA::cudart)
endif()

profiler_enable_sanitizer(ProfilerCxxTests)

set(_profiler_torch_found OFF)
if(PROFILER_ENABLE_LIBTORCH)
  find_package(Torch QUIET)
  if(Torch_FOUND)
    set(_profiler_torch_found ON)
    target_link_libraries(ProfilerCxxTests PRIVATE torch)
    target_compile_definitions(ProfilerCxxTests PRIVATE PROFILER_HAS_LIBTORCH=1)
  endif()
endif()
if(NOT _profiler_torch_found)
  target_compile_definitions(ProfilerCxxTests PRIVATE PROFILER_HAS_LIBTORCH=0)
endif()

if(CMAKE_INTERPROCEDURAL_OPTIMIZATION)
  set_target_properties(ProfilerCxxTests PROPERTIES INTERPROCEDURAL_OPTIMIZATION OFF)
  target_compile_options(ProfilerCxxTests PRIVATE -fno-lto)
  target_link_options(ProfilerCxxTests PRIVATE -fno-lto)
endif()

set_target_properties(ProfilerCxxTests PROPERTIES FOLDER "Tests")

add_test(NAME ProfilerCxxTests COMMAND ProfilerCxxTests)
set_tests_properties(ProfilerCxxTests PROPERTIES TIMEOUT 120 LABELS "Profiler;Unit")

if(COMMAND profiler_set_target_output_directories)
  profiler_set_target_output_directories(ProfilerCxxTests)
endif()
