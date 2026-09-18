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
    # BUILD_SHARED_LIBS belongs to the consuming project when this function is
    # loaded from an installed package.  Inspect the actual target instead so
    # a shared imported Profiler is never passed to a whole-archive linker
    # option merely because the consumer's default is static.
    get_target_property(_profiler_target_type ${target} TYPE)
    if(NOT _profiler_target_type STREQUAL "STATIC_LIBRARY")
        return()
    endif()

    # Wrapped in $<BUILD_INTERFACE:> so this self-referencing flag (it names
    # `target` itself, e.g. plain "Profiler") only applies to in-tree
    # consumers of the in-tree target. Without it, install(EXPORT) captures
    # this INTERFACE_LINK_OPTIONS entry verbatim -- unlike INTERFACE_LINK_LIBRARIES,
    # it isn't scanned/renamed to the namespaced "Profiler::Profiler" export
    # name, so an install consumer would get a broken generator expression
    # ("No target Profiler", reproduced during Phase 5.D's install(EXPORT)
    # migration). The installed package instead gets this same flag correctly
    # (against "Profiler::Profiler") from ProfilerConfig.cmake.in's own
    # explicit profiler_keep_static_registrations(Profiler::Profiler) call,
    # made fresh in the consumer's own scope after ProfilerTargets.cmake is
    # included.
    if(APPLE)
        target_link_options(
            ${target} INTERFACE "$<BUILD_INTERFACE:SHELL:-Wl,-force_load,$<TARGET_FILE:${target}>>"
        )
    elseif(MSVC)
        target_link_options(
            ${target} INTERFACE "$<BUILD_INTERFACE:/WHOLEARCHIVE:$<TARGET_FILE_NAME:${target}>>"
        )
    elseif(WIN32 AND NOT MINGW AND NOT CYGWIN)
        # The native Windows/MSVC ABI always links with an MSVC-style linker
        # (link.exe or lld-link), even when the compiler *frontend* isn't
        # MSVC-flavored -- plain clang++.exe defaults to the
        # x86_64-pc-windows-msvc target when invoked directly on Windows, but
        # (unlike clang-cl) still takes GNU-style command-line arguments, so a
        # raw "/WHOLEARCHIVE:..." token is misread as a file path
        # ("no such file or directory"); it must be forwarded verbatim via
        # -Xlinker instead. The previous code had no branch for this
        # (frontend, ABI) combination at all: CMake's MSVC variable reflects
        # the compiler frontend, not the ABI/linker, so this fell through to
        # the GNU branch below and emitted -Wl,--whole-archive/
        # -Wl,--no-whole-archive -- syntax lld-link does not understand.
        # lld-link only warns ("ignoring unknown argument") and links anyway,
        # so this silently dropped the self-registering GPU/host/python
        # tracer and CUDA/ITT stub translation units with no error
        # (reproduced: 36 test failures, zero link/configure errors). WIN32
        # is the target platform, so this is also correct for MSVC-targeting
        # cross-compiles; MINGW/CYGWIN are excluded since those really do
        # link with a GNU-style linker. Uses the full path
        # ($<TARGET_FILE:>, not just $<TARGET_FILE_NAME:> as the MSVC branch
        # above uses) -- link.exe resolves a bare "/WHOLEARCHIVE:name" against
        # libraries already given on the link line by matching base name, but
        # lld-link invoked this way (via clang's driver, not cl.exe) does not:
        # reproduced as "lld-link: error: could not open 'Profiler.lib': no
        # such file or directory" with just the base name.
        target_link_options(
            ${target} INTERFACE
            "$<BUILD_INTERFACE:SHELL:-Xlinker /WHOLEARCHIVE:$<TARGET_FILE:${target}>>"
        )
    elseif(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        target_link_options(
            ${target}
            INTERFACE
            "$<BUILD_INTERFACE:SHELL:-Wl,--whole-archive $<TARGET_FILE:${target}> -Wl,--no-whole-archive>"
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
