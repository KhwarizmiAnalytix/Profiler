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

/* Copyright 2018 The OpenXLA Authors.

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

#include <memory>  // for make_unique, unique_ptr

#include "common/profiler_macros.h"          // for PROFILER_UNUSED
#include "native/core/profiler_factory.h"    // for register_profiler_factory
#include "native/core/profiler_interface.h"  // for profiler_interface
#include "native/core/profiler_options.h"    // for profile_options
#include "native/exporters/xplane/xplane.h"  // for x_space

namespace profiler::profiler_impl
{
namespace
{

// MetadataCollector collect miscellaneous metadata for xprof, e.g. HLO protos
// from XLA runtime etc.
//
// Thread-safety: This class is go/thread-compatible.
class MetadataCollector : public profiler_interface
{
public:
    MetadataCollector() = default;

    profiler_status start() override
    {
        trace_active_ = true;
        return profiler_status::Ok();
    }

    profiler_status stop() override
    {
        trace_active_ = false;
        return profiler_status::Ok();
    }

    profiler_status collect_data(x_space* /*space*/) override { return profiler_status::Ok(); }

    MetadataCollector(const MetadataCollector&) = delete;
    void operator=(const MetadataCollector&)    = delete;

private:
    //std::vector<std::unique_ptr<xla::HloProto>> debug_info_;
    bool trace_active_ = false;
};

std::unique_ptr<profiler_interface> CreatMetadataCollector(const profile_options& options)
{
    return options.enable_hlo_proto() ? std::make_unique<MetadataCollector>() : nullptr;
}

}  // namespace

static auto register_metadata_collector_factory = []
{
    register_profiler_factory(&CreatMetadataCollector);
    return 0;
}();
}  // namespace profiler::profiler_impl
