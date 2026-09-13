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

#include <cstdint>
#include <memory>
#include <string>

#include "common/profiler_export.h"
#include "native/core/profiler_interface.h"
#include "native/exporters/xplane/xplane.h"
#include "native/tracing/tracing.h"

namespace profiler::profiler_impl
{

class threadpool_event_collector : public profiler::tracing::event_collector
{
public:
    threadpool_event_collector() = default;

    void record_event(uint64_t arg) const override;
    void start_region(uint64_t arg) const override;
    void stop_region() const override;
};

class threadpool_profiler_interface : public profiler_interface
{
public:
    threadpool_profiler_interface() = default;

    profiler_status start() override;
    profiler_status stop() override;
    profiler_status collect_data(x_space* space) override;

private:
    profiler_status last_status_ = profiler_status::Ok();
};

std::unique_ptr<profiler_interface> create_threadpool_profiler();

}  // namespace profiler::profiler_impl
