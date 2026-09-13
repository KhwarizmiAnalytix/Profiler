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

/* Copyright 2021 The TensorFlow Authors. All Rights Reserved.

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
#include <functional>
#include <string>
#include <utility>

#include "common/profiler_export.h"

namespace profiler
{

// Annotations for memory profiling and debugging purpose.
// scoped_memory_debug_annotation will cache the annotations in thread-local
// memory, and some allocators will try to tag allocations with the annotations.
struct memory_debug_annotation
{
    const char* pending_op_name     = nullptr;
    int64_t     pending_step_id     = 0;
    const char* pending_region_type = nullptr;
    int32_t     pending_data_type   = 0;
    // A lambda function, when invoked, it will generate the string that describe
    // the shape of the pending tensor. By default, the TensorShape string is an
    // empty string.
    std::function<std::string()> pending_shape_func = []() { return ""; };
};

// Wrapper class of memory_debug_annotation for RAII.
class scoped_memory_debug_annotation
{
public:
    static const memory_debug_annotation& current_annotation()
    {
        return *thread_memory_debug_annotation();
    }

    explicit scoped_memory_debug_annotation(const char* op_name)
    {
        memory_debug_annotation* thread_local_annotation = thread_memory_debug_annotation();
        last_annotation_                                 = *thread_local_annotation;
        *thread_local_annotation                         = memory_debug_annotation();
        thread_local_annotation->pending_op_name         = op_name;
    }

    explicit scoped_memory_debug_annotation(const char* op_name, int64_t step_id)
    {
        memory_debug_annotation* thread_local_annotation = thread_memory_debug_annotation();
        last_annotation_                                 = *thread_local_annotation;
        *thread_local_annotation                         = memory_debug_annotation();
        thread_local_annotation->pending_op_name         = op_name;
        thread_local_annotation->pending_step_id         = step_id;
    }

    // This constructor keeps the pending_op_name and pending_step_id from parent
    // (if any).  Otherwise it overwrites with op_name.
    explicit scoped_memory_debug_annotation(
        const char*                    op_name,
        const char*                    region_type,
        int32_t                        data_type,
        std::function<std::string()>&& pending_shape_func)
    {
        memory_debug_annotation* thread_local_annotation = thread_memory_debug_annotation();
        last_annotation_                                 = *thread_local_annotation;
        if (!thread_local_annotation->pending_op_name)
        {
            thread_local_annotation->pending_op_name = op_name;
        }
        thread_local_annotation->pending_region_type = region_type;
        thread_local_annotation->pending_data_type   = data_type;
        thread_local_annotation->pending_shape_func  = std::move(pending_shape_func);
    }

    explicit scoped_memory_debug_annotation(
        const char*                    op_name,
        int64_t                        step_id,
        const char*                    region_type,
        int32_t                        data_type,
        std::function<std::string()>&& pending_shape_func)
    {
        memory_debug_annotation* thread_local_annotation = thread_memory_debug_annotation();
        last_annotation_                                 = *thread_local_annotation;
        thread_local_annotation->pending_op_name         = op_name;
        thread_local_annotation->pending_step_id         = step_id;
        thread_local_annotation->pending_region_type     = region_type;
        thread_local_annotation->pending_data_type       = data_type;
        thread_local_annotation->pending_shape_func      = std::move(pending_shape_func);
    }

    ~scoped_memory_debug_annotation() { *thread_memory_debug_annotation() = last_annotation_; }

private:
    // Returns a pointer to the memory_debug_annotation for the current thread.
    PROFILER_API static memory_debug_annotation* thread_memory_debug_annotation();

    // Stores the previous values in case the annotations are nested.
    memory_debug_annotation last_annotation_;

    scoped_memory_debug_annotation(const scoped_memory_debug_annotation&)            = delete;
    scoped_memory_debug_annotation& operator=(const scoped_memory_debug_annotation&) = delete;
};

}  // namespace profiler
