# Compiler requirements and per-target static-analysis checks.

function(profiler_check_compiler)
    # Validate compiler minimums on every configure, including when feature checks are cached.
    # The floor scales with the selected C++ standard: each later standard needs a strictly
    # newer compiler to even recognize the -std=/-std:c++NN flag, let alone support the
    # language — a compiler that only clears the C++17 floor fails later with a raw
    # "unrecognized command line option" instead of this clear FATAL_ERROR.
    if(DEFINED PROFILER_CXX_STANDARD AND NOT PROFILER_CXX_STANDARD STREQUAL "")
        set(_profiler_checks_min_standard "${PROFILER_CXX_STANDARD}")
    elseif(DEFINED CMAKE_CXX_STANDARD AND NOT CMAKE_CXX_STANDARD STREQUAL "")
        set(_profiler_checks_min_standard "${CMAKE_CXX_STANDARD}")
    else()
        set(_profiler_checks_min_standard 17)
    endif()

    if(_profiler_checks_min_standard STREQUAL "23")
        # -std=c++23 (previously -std=c++2b).
        # https://gcc.gnu.org/gcc-11/changes.html
        set(_profiler_checks_min_gcc 11.0)
        # https://releases.llvm.org/17.0.0/tools/clang/docs/ReleaseNotes.html
        set(_profiler_checks_min_clang 17.0)
        # Apple has not published an explicit minimum for -std=c++23; best available estimate
        # — verify against CI before relying on it.
        set(_profiler_checks_min_appleclang 15.0)
        # /std:c++23preview from VS 17.0 (19.30); stable /std:c++23 only in very recent toolsets.
        # https://learn.microsoft.com/en-us/cpp/build/reference/std-specify-language-standard-version
        set(_profiler_checks_min_msvc 19.34)
    elseif(_profiler_checks_min_standard STREQUAL "20")
        # -std=c++20 (previously only -std=c++2a).
        # https://gcc.gnu.org/gcc-10/changes.html
        set(_profiler_checks_min_gcc 10.0)
        # https://releases.llvm.org/11.0.0/tools/clang/docs/ReleaseNotes.html
        set(_profiler_checks_min_clang 11.0)
        # Xcode 12 (AppleClang 12) still rejects -std=c++20 ("use c++2a"); Xcode 14
        # (AppleClang 14) accepts it — 13 is a conservative floor pending CI confirmation.
        set(_profiler_checks_min_appleclang 13.0)
        # https://devblogs.microsoft.com/cppblog/msvc-cpp20-and-the-std-cpp20-switch/
        set(_profiler_checks_min_msvc 19.29)
    else()
        # C++17 baseline. Floor is set by <filesystem> (used unconditionally by Logging's
        # string_util.h), not just language support: GCC 7.1/Clang 5.0 only have
        # <experimental/filesystem> and lack the <filesystem> header entirely ("fatal error:
        # filesystem: No such file or directory"), and GCC < 9.1 / Clang(LLVM) < 9.0 need an
        # extra -lstdc++fs/-lc++fs link flag this build does not add.
        # https://gcc.gnu.org/gcc-9/changes.html
        set(_profiler_checks_min_gcc 9.1)
        # https://releases.llvm.org/9.0.0/projects/libcxx/docs/UsingLibcxx.html
        set(_profiler_checks_min_clang 9.0)
        # AppleClang 11 / Xcode 11 is the first to ship the non-experimental <filesystem>
        # header; AppleClang 10 / Xcode 10 only has <experimental/filesystem>.
        # https://en.cppreference.com/w/cpp/compiler_support/17
        set(_profiler_checks_min_appleclang 11.0)
        # MSVC 19.14 / VS 2017 15.7 also supports /Zc:__cplusplus, used by platform.cmake.
        # https://learn.microsoft.com/en-us/cpp/overview/what-s-new-for-cpp-2017
        set(_profiler_checks_min_msvc 19.14)
    endif()

    if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU" AND CMAKE_CXX_COMPILER_VERSION VERSION_LESS
                                                _profiler_checks_min_gcc
    )
        message(
            FATAL_ERROR
                "GCC ${_profiler_checks_min_gcc} or later is required for C++${_profiler_checks_min_standard}. "
                "Found: ${CMAKE_CXX_COMPILER_VERSION}"
        )
    endif()

    if(CMAKE_CXX_COMPILER_ID STREQUAL "Clang" AND CMAKE_CXX_COMPILER_VERSION VERSION_LESS
                                                  _profiler_checks_min_clang
    )
        message(
            FATAL_ERROR
                "LLVM Clang ${_profiler_checks_min_clang} or later is required for C++${_profiler_checks_min_standard}. "
                "Found: ${CMAKE_CXX_COMPILER_VERSION}"
        )
    endif()

    if(CMAKE_CXX_COMPILER_ID STREQUAL "AppleClang" AND CMAKE_CXX_COMPILER_VERSION VERSION_LESS
                                                       _profiler_checks_min_appleclang
    )
        message(
            FATAL_ERROR
                "Apple Clang ${_profiler_checks_min_appleclang} or later is required for C++${_profiler_checks_min_standard}. "
                "Found: ${CMAKE_CXX_COMPILER_VERSION}"
        )
    endif()

    if(CMAKE_CXX_COMPILER_ID STREQUAL "MSVC" AND CMAKE_CXX_COMPILER_VERSION VERSION_LESS
                                                 _profiler_checks_min_msvc
    )
        message(
            FATAL_ERROR
                "MSVC ${_profiler_checks_min_msvc} or later is required for C++${_profiler_checks_min_standard}. "
                "Found: ${CMAKE_CXX_COMPILER_VERSION}"
        )
    endif()

    unset(_profiler_checks_min_standard)
    unset(_profiler_checks_min_gcc)
    unset(_profiler_checks_min_clang)
    unset(_profiler_checks_min_appleclang)
    unset(_profiler_checks_min_msvc)
endfunction()

# Static-analysis and include-hygiene checks for the Profiler target.
# Applied as per-target properties so third-party subdirectories are unaffected.
# Enable via -DPROFILER_ENABLE_CLANG_TIDY=ON / CPPCHECK / IWYU.

function(profiler_enable_checks target)
    if(PROFILER_ENABLE_CLANG_TIDY)
        find_program(PROFILER_CLANG_TIDY_EXECUTABLE NAMES clang-tidy)
        if(PROFILER_CLANG_TIDY_EXECUTABLE)
            set_target_properties(
                ${target} PROPERTIES CXX_CLANG_TIDY "${PROFILER_CLANG_TIDY_EXECUTABLE}"
            )
            message(STATUS "Profiler: clang-tidy enabled (${PROFILER_CLANG_TIDY_EXECUTABLE})")
        else()
            message(
                WARNING "PROFILER_ENABLE_CLANG_TIDY=ON but clang-tidy was not found — check skipped"
            )
        endif()
    endif()

    if(PROFILER_ENABLE_CPPCHECK)
        find_program(PROFILER_CPPCHECK_EXECUTABLE NAMES cppcheck)
        if(PROFILER_CPPCHECK_EXECUTABLE)
            set_target_properties(
                ${target}
                PROPERTIES
                    CXX_CPPCHECK
                    "${PROFILER_CPPCHECK_EXECUTABLE};--enable=warning,style,performance,portability;\
--suppress=missingIncludeSystem;--inline-suppr;--error-exitcode=1"
            )
            message(STATUS "Profiler: cppcheck enabled (${PROFILER_CPPCHECK_EXECUTABLE})")
        else()
            message(
                WARNING "PROFILER_ENABLE_CPPCHECK=ON but cppcheck was not found — check skipped"
            )
        endif()
    endif()

    if(PROFILER_ENABLE_IWYU)
        find_program(
            PROFILER_IWYU_EXECUTABLE NAMES include-what-you-use iwyu
            DOC "include-what-you-use binary"
        )
        if(PROFILER_IWYU_EXECUTABLE)
            set_target_properties(
                ${target}
                PROPERTIES CXX_INCLUDE_WHAT_YOU_USE
                           "${PROFILER_IWYU_EXECUTABLE};-Xiwyu;--no_fwd_decls;-Xiwyu;--cxx17ns"
            )
            message(STATUS "Profiler: include-what-you-use enabled (${PROFILER_IWYU_EXECUTABLE})")
        else()
            message(
                WARNING
                    "PROFILER_ENABLE_IWYU=ON but include-what-you-use was not found — check skipped"
            )
        endif()
    endif()
endfunction()
