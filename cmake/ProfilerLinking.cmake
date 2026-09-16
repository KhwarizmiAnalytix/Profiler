# Keeps self-registering static-init translation units alive when Profiler is
# consumed as a static library.
#
# Several sources register a capability purely via a file-scope static
# initializer with no other symbol referenced by the rest of the library
# (native/gpu/gpu_tracer_factory.cpp, native/cpu/host_tracer_factory.cpp,
# native/cpu/python_tracer_factory.cpp, native/cpu/metadata_collector.cpp,
# bespoke/base/cuda.cpp's RegisterCUDAOrHIPMethods, bespoke/itt/itt.cpp's
# RegisterITTMethods). A shared library always links every object file into
# the .so, so this only matters for a static build: an ordinary archive link
# only pulls in .o members that resolve an otherwise-undefined symbol, so a
# consumer that never directly references anything in one of those files gets
# the archive member -- and its registration -- silently dropped. Reproduced
# locally: a minimal consumer linked against a static Profiler with no
# force-link produced an empty capture (host tracer never registered) with no
# error. BUILD.bazel already avoids this via alwayslink = True; this is the
# CMake equivalent.
function(profiler_keep_static_registrations target)
    if(BUILD_SHARED_LIBS)
        return()
    endif()

    if(APPLE)
        target_link_options(
            ${target} INTERFACE "SHELL:-Wl,-force_load,$<TARGET_FILE:${target}>"
        )
    elseif(MSVC)
        target_link_options(${target} INTERFACE "/WHOLEARCHIVE:$<TARGET_FILE_NAME:${target}>")
    elseif(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        target_link_options(
            ${target} INTERFACE
            "SHELL:-Wl,--whole-archive $<TARGET_FILE:${target}> -Wl,--no-whole-archive"
        )
    else()
        message(
            WARNING
                "${target}: static build on ${CMAKE_CXX_COMPILER_ID} has no known whole-archive "
                "flag here -- consumers that don't reference gpu/host/python tracer or CUDA/ITT "
                "stub registration symbols directly may silently lose that capability. "
                "See cmake/ProfilerLinking.cmake."
        )
    endif()
endfunction()
