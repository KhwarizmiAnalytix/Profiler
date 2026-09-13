# Optional sanitizer instrumentation (GCC/Clang). Not supported on MSVC in this
# build matrix; see the "Sanitizers" section of docs/profiler.md.

function(profiler_enable_sanitizer target)
  if(NOT PROFILER_SANITIZER)
    return()
  endif()
  if(NOT (CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang"))
    message(WARNING "PROFILER_SANITIZER has no effect with ${CMAKE_CXX_COMPILER_ID}; "
                     "sanitizers require GCC or Clang"
    )
    return()
  endif()
  target_compile_options(
    ${target} PRIVATE -fsanitize=${PROFILER_SANITIZER} -fno-omit-frame-pointer -g -O1
  )
  target_link_options(${target} PRIVATE -fsanitize=${PROFILER_SANITIZER})
endfunction()
