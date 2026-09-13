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
