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

// Phase 6.B (design-review.md section 7, Phase 6): "Ensure public options
// have a tested effect or return a documented unsupported status." Phase 5
// already did the one-time audit (docs/phase-5-remaining.md item 7) --
// every field of session_options/capture_config/profiler_options currently
// has a traceable write site and read site (or an explicit "no effect"
// disclaimer, for the few that are intentionally inert -- none remain as
// of Phase 5.B's removals). This file is the *standing* check Phase 6 asks
// for: a compile-time tripwire that forces this cross-reference table to be
// re-checked whenever a field is added or removed, instead of the audit
// silently going stale.
//
// The tripwire uses structured bindings: decomposing an aggregate requires
// *exactly* as many bindings as it has members, so adding or removing a
// field breaks the build here with a clear "decomposes into N elements, but
// M names were provided" error -- a real compile-time enforcement, not a
// runtime check that could bit-rot unnoticed. C++ has no reflection, so
// this can't automatically verify *what* changed; it only guarantees a
// human looks at this file (and updates both the binding list and the
// cross-reference table below) before the build passes again.
//
// Cross-reference table (field -> where its effect or documented rejection
// is tested):
//
// session_options (common/session.h):
//   native                -> common/session.cpp's session::start()/stop()
//                             gate the native pipeline on this.
//   instrumentation        -> same; also
//                             TestProfilerBackendCapabilities.cpp's
//                             compiled_flags_match_this_build.
//   backend                -> to_capture_config() forwards it;
//                             TestProfilerBackendGpuFallback.cpp.
//   activities              -> to_capture_config() forwards it;
//                             TestProfilerBackendCapabilities.cpp's
//                             cuda_and_hip_require_a_real_device.
//   memory_tracking         -> native/session/profiler.cpp's options
//                             translation; TestProfilerBackendMemory.cpp.
//   gpu_tracing             -> same; TestProfilerGpuTracer.cpp /
//                             TestProfilerGpuRealHardware.cpp.
//   profile_memory          -> to_capture_config(); bespoke observer/kineto
//                             config gating (see stats_calculator.h's
//                             consumers); TestProfilerBackendMemory.cpp.
//   with_stack              -> to_capture_config(); bespoke/common/
//                             collection.cpp reads it directly;
//                             TestProfilerHeavyFunction.cpp (stack capture).
//   report_input_shapes     -> to_capture_config(); nvtx_observer.cpp /
//                             profiler_kineto.cpp / itt_observer.cpp all
//                             read it directly.
//   with_flops              -> to_capture_config(); threaded into
//                             libkineto's own RecordFunction config via
//                             kineto_client_interface.cpp (vendored
//                             consumer, not first-party-testable directly).
//   with_modules            -> same path as with_flops.
//   statistical_analysis    -> native/session/profiler.cpp's
//                             enable_statistical_analysis_ translation;
//                             TestStatisticalAnalysis.cpp.
//   policy                  -> common/session.cpp's required-vs-best-effort
//                             gate; TestProfilerBackendCapabilities.cpp's
//                             required_unavailable_gpu_activity_fails_start
//                             and best_effort's sibling test (documented,
//                             tested "unsupported" status for `required`).
//
// capture_config (common/capture.h):
//   backend, activities, profile_memory, with_stack, report_input_shapes,
//   with_flops, with_modules -> same evidence as session_options' fields of
//   the same name (session_options::to_capture_config() maps 1:1 onto
//   these).
//
// profiler_options (native/session/profiler.h):
//   enable_timing_               -> profiler_session construction path;
//                                   TestProfilerNativeHotspot.cpp.
//   enable_memory_tracking_       -> TestProfilerBackendMemory.cpp.
//   enable_hierarchical_profiling_ -> TestScopeTreeBuilder.cpp.
//   enable_statistical_analysis_  -> TestStatisticalAnalysis.cpp.
//   enable_gpu_tracing_           -> TestProfilerGpuTracer.cpp.
//   output_format_                -> profiler_report.cpp's format
//                                   selection; TestProfilerBackendOutput.cpp.
//   max_samples_                  -> statistical_analyzer's
//                                   max_samples_per_series_ wiring;
//                                   TestStatisticalAnalysis.cpp.
//   track_memory_deltas_          -> TestProfilerBackendMemory.cpp.
//
// To pick this up when the compile error fires: identify the added/removed
// field, update the binding list below to match the struct's current
// member count, and update the cross-reference table above with either the
// new field's test evidence or a documented-rejection test proving it's an
// explicit no-op (design-review.md's "or return a documented unsupported
// status").

#include "common/capture.h"
#include "common/session.h"
#include "native/session/profiler.h"

namespace
{

[[maybe_unused]] void session_options_field_count_tripwire()
{
    profiler::session_options opts;
    auto& [native,
        instrumentation,
        backend,
        activities,
        memory_tracking,
        gpu_tracing,
        profile_memory,
        with_stack,
        report_input_shapes,
        with_flops,
        with_modules,
        statistical_analysis,
        policy] = opts;
    (void)native;
    (void)instrumentation;
    (void)backend;
    (void)activities;
    (void)memory_tracking;
    (void)gpu_tracing;
    (void)profile_memory;
    (void)with_stack;
    (void)report_input_shapes;
    (void)with_flops;
    (void)with_modules;
    (void)statistical_analysis;
    (void)policy;
}

[[maybe_unused]] void capture_config_field_count_tripwire()
{
    profiler::capture_config cfg;
    auto& [backend,
        activities,
        profile_memory,
        with_stack,
        report_input_shapes,
        with_flops,
        with_modules] = cfg;
    (void)backend;
    (void)activities;
    (void)profile_memory;
    (void)with_stack;
    (void)report_input_shapes;
    (void)with_flops;
    (void)with_modules;
}

[[maybe_unused]] void profiler_options_field_count_tripwire()
{
    profiler::profiler_options opts;
    auto& [enable_timing,
        enable_memory_tracking,
        enable_hierarchical_profiling,
        enable_statistical_analysis,
        enable_gpu_tracing,
        output_format,
        max_samples,
        track_memory_deltas] = opts;
    (void)enable_timing;
    (void)enable_memory_tracking;
    (void)enable_hierarchical_profiling;
    (void)enable_statistical_analysis;
    (void)enable_gpu_tracing;
    (void)output_format;
    (void)max_samples;
    (void)track_memory_deltas;
}

}  // namespace
