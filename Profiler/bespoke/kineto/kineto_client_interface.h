#pragma once

// TODO: Missing Profiler dependency - original include was:
// #include <profiler/csrc/jit/runtime/interpreter.h>
// This is a Profiler-specific header not available in Profiler

#include "common/profiler_export.h"

namespace profiler
{

// declare global_kineto_init for libtorch_cpu.so to call
PROFILER_API void global_kineto_init();

}  // namespace profiler
