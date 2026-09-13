# Prove FetchContent_MakeAvailable(XSigmaProfiler) from an external project.
cmake_minimum_required(VERSION 3.16)
project(XSigmaProfilerFetchConsumer LANGUAGES CXX)

include(FetchContent)
FetchContent_Declare(
  XSigmaProfiler
  SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../.."
)
set(PROFILER_ENABLE_TESTING OFF CACHE BOOL "" FORCE)
set(PROFILER_ENABLE_INSTALL OFF CACHE BOOL "" FORCE)
if(DEFINED ENV{PROFILER_THIRD_PARTY_DIR} AND NOT PROFILER_THIRD_PARTY_DIR)
  set(PROFILER_THIRD_PARTY_DIR "$ENV{PROFILER_THIRD_PARTY_DIR}" CACHE PATH "" FORCE)
endif()
FetchContent_MakeAvailable(XSigmaProfiler)

add_executable(profiler_fetch_consumer ../main.cpp)
target_link_libraries(profiler_fetch_consumer PRIVATE Profiler::Profiler)
target_compile_features(profiler_fetch_consumer PRIVATE cxx_std_20)
