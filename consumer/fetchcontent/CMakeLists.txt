# Prove FetchContent from an external project. Local SOURCE_DIR keeps this
# dummy fast. Other repos should declare:
#   GIT_REPOSITORY https://github.com/KhwarizmiAnalytix/Profiler.git
#   GIT_TAG        main
cmake_minimum_required(VERSION 3.16)
project(ProfilerFetchConsumer LANGUAGES CXX)

include(FetchContent)
FetchContent_Declare(
  Profiler
  SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../.."
)
set(PROFILER_ENABLE_TESTING OFF CACHE BOOL "" FORCE)
set(PROFILER_ENABLE_INSTALL OFF CACHE BOOL "" FORCE)
if(DEFINED ENV{PROFILER_THIRD_PARTY_DIR} AND NOT PROFILER_THIRD_PARTY_DIR)
  set(PROFILER_THIRD_PARTY_DIR "$ENV{PROFILER_THIRD_PARTY_DIR}" CACHE PATH "" FORCE)
endif()
FetchContent_MakeAvailable(Profiler)

add_executable(profiler_fetch_consumer ../main.cpp)
target_link_libraries(profiler_fetch_consumer PRIVATE Profiler::Profiler)
target_compile_features(profiler_fetch_consumer PRIVATE cxx_std_20)
