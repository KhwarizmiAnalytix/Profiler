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

#include "bespoke/common/orchestration/observer.h"

// There are some components which use these symbols. Until we migrate them
// we have to mirror them in the old autograd namespace.

// TODO: Profiler-specific types commented out
namespace profiler::profiler_impl
{
using profiler::profiler_impl::impl::ActivityType;
using profiler::profiler_impl::impl::getProfilerConfig;
using profiler::profiler_impl::impl::ProfilerConfig;
using profiler::profiler_impl::impl::profilerEnabled;
using profiler::profiler_impl::impl::ProfilerState;
}  // namespace profiler::profiler_impl
