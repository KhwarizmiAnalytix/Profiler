# Tiny external consumer used to prove find_package(XSigmaProfiler).
cmake_minimum_required(VERSION 3.16)
project(XSigmaProfilerConsumer LANGUAGES CXX)

find_package(XSigmaProfiler REQUIRED)

add_executable(profiler_consumer main.cpp)
target_link_libraries(profiler_consumer PRIVATE Profiler::Profiler)
target_compile_features(profiler_consumer PRIVATE cxx_std_20)
