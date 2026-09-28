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
