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

/* Copyright 2018 The TensorFlow Authors. All Rights Reserved.

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

namespace profiler
{
// Used to control the output of the statistics summarizer;
struct stat_summarizer_options
{
    stat_summarizer_options()
        : show_run_order(true),
          run_order_limit(0),
          show_time(true),
          time_limit(10),
          show_memory(true),
          memory_limit(10),
          show_type(true),
          show_summary(true),
          format_as_csv(false)
    {
    }

    bool show_run_order;
    int  run_order_limit;
    bool show_time;
    int  time_limit;
    bool show_memory;
    int  memory_limit;
    bool show_type;
    bool show_summary;
    bool format_as_csv;
};
}  // namespace profiler
