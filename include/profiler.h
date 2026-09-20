#pragma once

/**
 * Public C++ API. Application code includes this header and links
 * Profiler::Profiler. Native, Kineto, and ITT stay inside the library.
 *
 *   #include "profiler.h"
 *
 *   profiler::session_options options;
 *   options.backend = profiler::capture_backend::automatic;
 *   options.activities = {profiler::activity::cpu};
 *   profiler::session session(options);
 *   session.start();
 *   { PROFILER_SCOPE("work"); }
 *   session.stop();
 *   session.write_trace("trace.json");
 */

#include "common/capture.h"
#include "common/instrumentation.h"
#include "common/session.h"
#include "native/memory/memory_tracker.h"
#include "native/session/profiler.h"
#include "native/session/profiler_report.h"
