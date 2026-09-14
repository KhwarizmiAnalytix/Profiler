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

#include <memory>

#include "common/profiler_export.h"

namespace profiler
{
class x_space;
struct profiler_scope_data;

namespace scope_tree_builder
{

/**
 * @brief Reconstructs a hierarchical scope view over a collected XSpace.
 *
 * Hierarchy is treated as a *derived view* over the flat traceme/host_tracer
 * event log, not a structure tracked live during collection -- matching how
 * the TF/XLA profiler treats it (TraceMeRecorder records a flat, per-thread
 * event stream; any tree shape is a post-processing step over that log,
 * computed once when needed rather than maintained under a shared lock on
 * every scope start/end).
 *
 * Events on each host-thread XLine are nested by interval containment: an
 * event becomes a child of the innermost still-open event whose [start, end)
 * range contains it. `space` is expected to already have normalized,
 * session-relative timestamps (as produced by
 * profiler_session::normalize_xspace()) -- this function does not shift
 * them further.
 *
 * @param space Collected XSpace (see profiler_session::collected_xspace()).
 * @return Owning pointer to a synthetic "ROOT" node whose descendants mirror
 *         the recorded scope nesting, or nullptr if `space` has no
 *         host-thread events to build a tree from.
 */
PROFILER_API std::unique_ptr<profiler::profiler_scope_data> build_scope_tree(const profiler::x_space& space);

}  // namespace scope_tree_builder
}  // namespace profiler
