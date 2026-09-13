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

#include <array>
#include <atomic>
#include <cstdint>
#include <string_view>

#include "common/profiler_export.h"

namespace profiler::tracing
{

// Identifiers for CPU profiler events emitted through the tracing interface.
enum class event_category : unsigned
{
    kScheduleClosure = 0,
    kRunClosure      = 1,
    kCompute         = 2,
    kNumCategories   = 3  // sentinel - keep last
};

PROFILER_API const char* get_event_category_name(event_category category);

// Interface for CPU profiler events.
class PROFILER_VISIBILITY event_collector
{
public:
    virtual ~event_collector() = default;

    virtual void record_event(uint64_t arg) const = 0;
    virtual void start_region(uint64_t arg) const = 0;
    virtual void stop_region() const              = 0;

    PROFILER_API static void set_current_thread_name(const char* name);
    static bool              is_enabled();

private:
    friend PROFILER_API void set_event_collector(
        event_category category, const event_collector* collector);
    friend PROFILER_API const event_collector* get_event_collector(event_category category);

    static std::array<const event_collector*, static_cast<unsigned>(event_category::kNumCategories)>
        instances_;
};

// Registers a collector for the provided category.
PROFILER_API void set_event_collector(event_category category, const event_collector* collector);

// Returns the active collector for the category if tracing is enabled.
PROFILER_API const event_collector* get_event_collector(event_category category);

// Utility helpers for generating identifiers passed to collectors.
PROFILER_API uint64_t get_unique_arg();
PROFILER_API uint64_t get_arg_for_name(std::string_view name);

// Records an instant event through the registered collector.
PROFILER_API void record_event(event_category category, uint64_t arg);

// Records a region through the registered collector for the lifetime of the instance.
class PROFILER_VISIBILITY scoped_region
{
public:
    PROFILER_API scoped_region(event_category category, uint64_t arg);
    PROFILER_API explicit scoped_region(event_category category);
    PROFILER_API scoped_region(event_category category, std::string_view name);
    PROFILER_API scoped_region(scoped_region&& other) noexcept;
    PROFILER_API ~scoped_region();

    bool is_enabled() const { return collector_ != nullptr; }

private:
    scoped_region(const scoped_region&)            = delete;
    scoped_region& operator=(const scoped_region&) = delete;

    const event_collector* collector_ = nullptr;
};

// Return the pathname of the directory where profiler logs are written.
PROFILER_API const char* get_log_dir();

}  // namespace profiler::tracing

#include "native/tracing/tracing_impl.h"
