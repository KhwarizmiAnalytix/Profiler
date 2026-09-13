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

#include <cstddef>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

#include "common/profiler_export.h"

namespace profiler
{

struct profiler_scope_data;

/**
 * @brief One aggregated hotspot: every call site sharing the same
 * profiler_scope / TraceMe name, merged the way VTune's "Bottom-up" view
 * (and Kineto's hotspot_report) merges samples by function.
 *
 * CPU only — native TraceMe scopes do not carry CUDA/XPU device times.
 */
struct PROFILER_VISIBILITY hotspot_entry
{
    std::string name;
    uint64_t    self_time_ns  = 0;  ///< CPU self time: this scope excluding children.
    uint64_t    total_time_ns = 0;  ///< CPU inclusive time: this call plus descendants.
    uint64_t    call_count    = 0;
};

/**
 * @brief VTune / Kineto-style top-down and bottom-up hotspot breakdown built
 * from the native scope tree (profiler_session::build_scope_tree()).
 *
 * Instrumentation-based (not PC sampling): nodes are profiler_scope /
 * PROFILER_PROFILE_SCOPE / TraceMe intervals reconstructed by
 * scope_tree_builder. Self/total time is exact for annotated sites; the
 * "call stack" is the nested-scope parent chain.
 *
 * Distinct from profiler::autograd::profiler_impl::hotspot_report (Kineto
 * ProfilerResult). Same CPU column semantics in table() / bottom_up_hotspots().
 */
class PROFILER_VISIBILITY hotspot_report
{
public:
    /**
     * @param root Scope tree root (typically the synthetic "ROOT" node from
     *             build_scope_tree()). Nullptr yields an empty report.
     */
    PROFILER_API explicit hotspot_report(const profiler_scope_data* root);

    /// Indented top-down call tree: name, self/total, % of root total, calls.
    PROFILER_API std::string top_down_tree() const;

    /// Merged by name, sorted by self time descending (VTune Hotspots order).
    /// @param max_rows Maximum rows (0 = all).
    PROFILER_API std::string bottom_up_hotspots(size_t max_rows = 20) const;

    /// First observed root → … → leaf path for `name`, if any.
    PROFILER_API std::vector<std::string> call_stack_for(const std::string& name) const;

    /**
     * @brief PyTorch key_averages()-style CPU table: Name, Self CPU %, Self CPU,
     * CPU total %, CPU total, CPU time avg, # of Calls.
     * @param sort_by Empty keeps self-CPU descending; also accepts
     *        self_cpu_time_total, cpu_time_total, cpu_time_avg, count.
     * @param row_limit Maximum data rows (0 = all).
     */
    PROFILER_API std::string table(const std::string& sort_by = "", size_t row_limit = 100) const;

    /// Hotspots sorted by self CPU time descending.
    const std::vector<hotspot_entry>& hotspots() const { return hotspots_; }

private:
    const profiler_scope_data*                                root_ = nullptr;
    std::vector<hotspot_entry>                                hotspots_;
    std::unordered_map<std::string, std::vector<std::string>> call_stacks_;
};

}  // namespace profiler
