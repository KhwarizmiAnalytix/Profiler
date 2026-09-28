/*
 * Profiler
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     https://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

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
