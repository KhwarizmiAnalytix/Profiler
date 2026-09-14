# Tiny external consumer used to prove find_package(Profiler).
cmake_minimum_required(VERSION 3.16)
project(ProfilerConsumer LANGUAGES CXX)

find_package(Profiler REQUIRED)

add_executable(profiler_consumer main.cpp)
target_link_libraries(profiler_consumer PRIVATE Profiler::Profiler)
target_compile_features(profiler_consumer PRIVATE cxx_std_20)
