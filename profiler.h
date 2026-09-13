#pragma once

/**
 * Public entry point for any C++ project.
 *
 * Typical use:
 *
 *   #include "profiler.h"
 *
 *   int main() {
 *     profiler::profiler_session session;
 *     session.start();
 *     {
 *       PROFILER_PROFILE_SCOPE("work");
 *       // ... application code ...
 *     }
 *     session.stop();
 *     session.write_chrome_trace("trace.json");  // chrome://tracing or Perfetto
 *   }
 *
 * Link CMake target Profiler::Profiler. No XSigma headers or libraries
 * are required.
 */

#include "common/instrumentation.h"
#include "native/session/profiler.h"
