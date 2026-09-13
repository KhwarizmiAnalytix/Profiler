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

#include <memory>

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
std::unique_ptr<profiler::profiler_scope_data> build_scope_tree(const profiler::x_space& space);

}  // namespace scope_tree_builder
}  // namespace profiler
