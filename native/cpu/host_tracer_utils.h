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

/* Copyright 2019 The TensorFlow Authors. All Rights Reserved.

Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at

    http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License.
==============================================================================*/

#pragma once

#include <cstdint>

#include "common/profiler_export.h"
#include "common/profiler_macros.h"
#include "native/exporters/xplane/xplane.h"
#include "native/tracing/traceme_recorder.h"

namespace profiler
{
namespace profiler_impl
{

/**
 * @brief Converts complete TraceMe events to XPlane format.
 *
 * This function takes a collection of TraceMe events from the traceme_recorder
 * and converts them into the XPlane format for visualization and analysis.
 * It handles:
 * - Event filtering (incomplete events, events before start time)
 * - Annotation parsing for metadata extraction
 * - Display name generation
 * - Event metadata and statistics
 * - Line sorting
 *
 * @param start_timestamp_ns The start timestamp in nanoseconds (events before this are filtered)
 * @param events The collection of TraceMe events to convert (moved)
 * @param raw_plane The XPlane to populate with converted events
 */
PROFILER_API void convert_complete_events_to_xplane(
    uint64_t start_timestamp_ns, traceme_recorder::Events&& events, xplane* raw_plane);

}  // namespace profiler_impl
}  // namespace profiler
