/*
 * XSigma: High-Performance Computational Library
 *
 * SPDX-License-Identifier: GPL-3.0-or-later OR Commercial
 *
 * This file is part of XSigma and is licensed under a dual-license model:
 *
 *   - Open-source License (GPLv3):
 *       Free for personal, academic, and research use under the terms of
 *       the GNU General Public License v3.0 or later.
 *
 *   - Commercial License:
 *       A commercial license is required for proprietary, closed-source,
 *       or SaaS usage. Contact us to obtain a commercial agreement.
 *
 * Contact: licensing@xsigma.co.uk
 * Website: https://www.xsigma.co.uk
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
 *   "displayTimeUnit": "ns"
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
 * **Time Units**: All timestamps are in nanoseconds (ns).
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
