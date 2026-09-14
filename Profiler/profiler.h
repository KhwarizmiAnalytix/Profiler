#pragma once

/**
 * Public C++ API. Application code includes this header and links
 * Profiler::Profiler. Native, Kineto, and ITT stay inside the library.
 *
 *   #include "profiler.h"
 *
 *   profiler::session session;
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
