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

#include "bespoke/kineto/profiler_kineto.h"
#include "common/profiler_export.h"

namespace profiler::profiler_impl
{

/**
 * @brief One aggregated hotspot: every call site sharing the same
 * PROFILER_RECORD_FUNCTION/PROFILER_RECORD_USER_SCOPE name, merged the way VTune's "Bottom-up"
 * view merges samples by function.
 */
struct PROFILER_VISIBILITY hotspot_entry
{
    std::string name;
    uint64_t    self_time_ns  = 0;  ///< CPU self time: this function excluding children.
    uint64_t    total_time_ns = 0;  ///< CPU inclusive time: this call plus CPU descendants.
    uint64_t    call_count    = 0;
    uint64_t    self_cuda_ns  = 0;  ///< CUDA/HIP self time, excluding GPU children.
    uint64_t    cuda_total_ns = 0;  ///< CUDA/HIP inclusive time.
    uint64_t    self_xpu_ns   = 0;  ///< XPU self time, excluding XPU children.
    uint64_t    xpu_total_ns  = 0;  ///< XPU inclusive time.
};

/**
 * @brief VTune-style top-down call tree / bottom-up hotspot breakdown, built
 * from the RecordFunction event tree captured by a Kineto profiling session.
 *
 * Unlike VTune's own sampling-based views, this is instrumentation-based:
 * nodes come from PROFILER_RECORD_FUNCTION/PROFILER_RECORD_USER_SCOPE scopes rather than PC
 * samples, so self/total time is exact for whatever call sites were
 * annotated, and the "call stack" for a hotspot is the RecordFunction parent
 * chain rather than a symbolized native stack trace.
 */
class PROFILER_VISIBILITY hotspot_report
{
public:
    PROFILER_API explicit hotspot_report(const ProfilerResult& result);

    /**
     * @brief Indented top-down call tree, one line per call site: name,
     * self/total time, percentage of that root's total time, and call count.
     * One tree is rendered per root (i.e. per thread's outermost scope).
     */
    PROFILER_API std::string top_down_tree() const;

    /**
     * @brief Functions merged across call sites, sorted by self time
     * descending -- VTune's default "Hotspots" sort order.
     * @param max_rows Maximum number of rows to include (0 = all).
     */
    PROFILER_API std::string bottom_up_hotspots(size_t max_rows = 20) const;

    /**
     * @brief The call path (root -> ... -> leaf) for the first occurrence of
     * the given function name, if it was observed. Mirrors VTune's
     * "Call Stack" pane for a selected hotspot.
     */
    PROFILER_API std::vector<std::string> call_stack_for(const std::string& name) const;

    /**
     * @brief PyTorch `key_averages().table()` breakdown: Name, Self CPU %,
     * Self CPU, CPU total %, CPU total, CPU time avg, optional CUDA/XPU
     * columns when those devices recorded time, and # of Calls.
     *
     * Matches the console table from torch.profiler / Intel Extension for
     * PyTorch (`sort_by`, `row_limit`). Percentages are relative to the sum
     * of self times on that device. `sort_by` is one of
     * `self_cpu_time_total`, `cpu_time_total`, `cpu_time_avg`,
     * `self_cuda_time_total`, `cuda_time_total`, `cuda_time_avg`,
     * `self_xpu_time_total`, `xpu_time_total`, `xpu_time_avg`, `count`.
     * Empty `sort_by` keeps the default self-CPU descending order.
     * @param row_limit Maximum data rows (0 = all).
     */
    PROFILER_API std::string table(const std::string& sort_by = "", size_t row_limit = 100) const;

    /// Hotspots sorted by self CPU time descending.
    const std::vector<hotspot_entry>& hotspots() const { return hotspots_; }

private:
    std::vector<experimental_event_t>                         roots_;
    std::vector<hotspot_entry>                                hotspots_;
    std::unordered_map<std::string, std::vector<std::string>> call_stacks_;
};

}  // namespace profiler::profiler_impl
