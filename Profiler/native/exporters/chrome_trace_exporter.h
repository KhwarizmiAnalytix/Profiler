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

#include <string>

#include "common/profiler_export.h"
#include "common/profiler_macros.h"
#include "native/exporters/xplane/xplane.h"

namespace profiler
{
namespace profiler_impl
{

// Phase 6.H (design-review.md section 7, Phase 6 / section 6.7: "Record
// schema/library/SDK versions... for reproducibility" / "Treat event
// schema/version and export compatibility as contracts"). Bumps only when
// this exporter's own emitted JSON *structure* changes in a way a consumer
// parsing it would need to know about (a field is renamed, removed, or
// changes meaning) -- not on every profiler library release. Consumers can
// read `metadata.profilerChromeTraceSchemaVersion` in the exported JSON
// (see export_to_chrome_trace_json()'s own doc comment for where it's
// emitted) to detect a format they don't understand instead of silently
// misparsing it. See TestProfilerXPlanePipeline.cpp's schema-version
// assertion, which fails if this constant changes without a corresponding,
// deliberate test update -- a compatibility change should never land
// silently.
inline constexpr int kChromeTraceSchemaVersion = 1;

/**
 * @brief Export profiling data to Chrome Trace Event Format (JSON).
 *
 * The Chrome Trace Event Format is a simple JSON format that can be viewed
 * in Chrome's built-in trace viewer (chrome://tracing) or in Perfetto UI.
 *
 * **Format Specification**:
 * https://docs.google.com/document/d/1CvAClvFfyA5R-PhYUmn5OOQtYMH4h6I0nSsKchNAySU
 *
 * **Supported Event Types**:
 * - Duration Events (X): Complete events with begin and end timestamps
 * - Instant Events (i): Point-in-time events
 * - Metadata Events (M): Process and thread names
 *
 * **Example Output**:
 * ```json
 * {
 *   "traceEvents": [
 *     {"name": "process_name", "ph": "M", "pid": 1, "args": {"name": "Host"}},
 *     {"name": "thread_name", "ph": "M", "pid": 1, "tid": 100, "args": {"name": "Worker-1"}},
 *     {"name": "compute", "ph": "X", "pid": 1, "tid": 100, "ts": 1000, "dur": 500},
 *     {"name": "event", "ph": "i", "pid": 1, "tid": 100, "ts": 1500, "s": "t"}
 *   ],
 *   "displayTimeUnit": "ns",
 *   "metadata": {"profilerChromeTraceSchemaVersion": 1}
 * }
 * ```
 *
 * **Usage**:
 * ```cpp
 * x_space space;
 * // ... populate space with profiling data ...
 *
 * std::string json = export_to_chrome_trace_json(space);
 * // Write json to file or send to UI
 * ```
 */

/**
 * @brief Export x_space to Chrome Trace Event Format JSON.
 *
 * Converts all planes, lines, and events in the x_space to Chrome Trace
 * Event Format JSON that can be viewed in chrome://tracing or Perfetto UI.
 *
 * **Time Units**: Timestamps and durations are in microseconds (us), as required
 * by Chrome Trace. displayTimeUnit is a presentation hint only.
 *
 * **Process/Thread Mapping**:
 * - Each XPlane becomes a process (pid)
 * - Each XLine becomes a thread (tid)
 * - Events are mapped to duration events (ph: "X")
 *
 * @param space The x_space containing profiling data
 * @param pretty_print If true, format JSON with indentation (default: false)
 * @return JSON string in Chrome Trace Event Format
 */
PROFILER_API std::string export_to_chrome_trace_json(
    const x_space& space, bool pretty_print = false);

/**
 * @brief Export x_space to Chrome Trace Event Format JSON file.
 *
 * Convenience function that exports to JSON and writes to a file.
 *
 * @param space The x_space containing profiling data
 * @param filename Output filename (e.g., "trace.json")
 * @param pretty_print If true, format JSON with indentation (default: false)
 * @return true if successful, false on error
 */
PROFILER_API bool export_to_chrome_trace_json_file(
    const x_space& space, const std::string& filename, bool pretty_print = false);

}  // namespace profiler_impl
}  // namespace profiler
