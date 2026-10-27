# =============================================================================
# XSigma Helper
# Macros Module
#
# Provides xsigma_target_optional_icecc() — opts a single target into the Icecream (icecc)
# distributed-compilation launcher when the caller's XXX_ENABLE_ICECC option is set.
# =============================================================================

include_guard(GLOBAL)

# Use Icecream (icecc) as the compiler launcher for a single target when XXX_ENABLE_ICECC is ON.
function(xsigma_target_optional_icecc target_name enable_var)
  if(NOT ${enable_var})
    return()
  endif()
  if(NOT TARGET "${target_name}")
    message(FATAL_ERROR "xsigma_target_optional_icecc: target '${target_name}' does not exist")
  endif()
  if(NOT XSIGMA_ICECC_EXECUTABLE)
    find_program(XSIGMA_ICECC_EXECUTABLE NAMES icecc)
  endif()
  if(XSIGMA_ICECC_EXECUTABLE)
    set_property(
      TARGET "${target_name}" PROPERTY CXX_COMPILER_LAUNCHER "${XSIGMA_ICECC_EXECUTABLE}"
    )
    set_property(TARGET "${target_name}" PROPERTY C_COMPILER_LAUNCHER "${XSIGMA_ICECC_EXECUTABLE}")
    message(STATUS "Icecream compiler launcher for ${target_name}: ${XSIGMA_ICECC_EXECUTABLE}")
  else()
    message(STATUS "Icecream enabled for ${target_name} but icecc was not found in PATH")
  endif()
endfunction()
