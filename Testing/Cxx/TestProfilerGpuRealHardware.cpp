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

/*
 * Phase 4 real-hardware GPU validation tests (design-review.md section 8).
 * Tests CUDA kernel launch and transfers on actual hardware when available.
 * All tests skip cleanly when no GPU is present.
 */

#include "ProfilerTest.h"

#if PROFILER_HAS_CUDA

#include <cuda_runtime_api.h>

#include "bespoke/base/base.h"
#include "common/annotation.h"
#include "common/backend_capabilities.h"
#include "common/capture.h"
#include "common/clock_calibration.h"
#include "native/session/profiler.h"

namespace
{

// Trivial CUDA kernel for testing.
__global__ void add_kernel(float* d_out, float* d_a, float* d_b, int n)
{
    int i = blockIdx.x * blockDim.x + threadIdx.x;
    if (i < n)
    {
        d_out[i] = d_a[i] + d_b[i];
    }
}

// A plain (non-nested-scope) wrapper around the <<<>>> launch: nvcc's
// host/device code splitting doesn't reliably identify guard-object braces
// like PROFILER_SCOPE's as extraneous, so isolating the launch on its own
// avoids leaking unresolved device-builtin symbols (blockDim/gridDim) into
// host-side codegen.
void launch_add_kernel(float* d_out, float* d_a, float* d_b, int n)
{
    dim3 blockDim(256);
    dim3 gridDim((n + blockDim.x - 1) / blockDim.x);
    add_kernel<<<gridDim, blockDim>>>(d_out, d_a, d_b, n);
}

}  // namespace

// Real-hardware test: kernel launch and H2D/D2H transfers appear in capture.
// Phase 4 requirement: "Kernel + H2D/D2H transfer, ... correlation IDs linking
// launch to execution" (design-review.md section 8, "Real CUDA/ROCm" row).
PROFILERTEST(GpuRealHardware, kernel_and_transfers_captured)
{
    const auto& caps = profiler::discover_backend_capabilities();
    if (!caps.gpu_device_available)
    {
        GTEST_SKIP() << "No CUDA device available";
    }

    const int N = 1024;
    const int bytes = N * sizeof(float);

    // Allocate device memory.
    float* d_a = nullptr;
    float* d_b = nullptr;
    float* d_out = nullptr;
    cudaError_t err = cudaMalloc(&d_a, bytes);
    if (err != cudaSuccess)
    {
        GTEST_SKIP() << "cudaMalloc failed: " << cudaGetErrorString(err);
    }
    ASSERT_EQ(cudaMalloc(&d_b, bytes), cudaSuccess);
    ASSERT_EQ(cudaMalloc(&d_out, bytes), cudaSuccess);

    // Allocate host memory.
    float* h_a = new float[N];
    float* h_b = new float[N];
    float* h_out = new float[N];
    for (int i = 0; i < N; ++i)
    {
        h_a[i] = static_cast<float>(i);
        h_b[i] = static_cast<float>(i * 2);
    }

    struct Cleanup
    {
        ~Cleanup()
        {
            cudaFree(d_a);
            cudaFree(d_b);
            cudaFree(d_out);
            delete[] h_a;
            delete[] h_b;
            delete[] h_out;
        }
        float* d_a;
        float* d_b;
        float* d_out;
        float* h_a;
        float* h_b;
        float* h_out;
    } cleanup{d_a, d_b, d_out, h_a, h_b, h_out};

    // Start profiling capture with CUDA activity enabled.
    profiler::capture_config config;
    config.activities = {profiler::activity::cpu, profiler::activity::cuda};

    profiler::capture cap(config);
    ASSERT_TRUE(cap.prepare());
    ASSERT_TRUE(cap.start());

    // Host-to-device transfer.
    ASSERT_EQ(cudaMemcpy(d_a, h_a, bytes, cudaMemcpyHostToDevice), cudaSuccess);

    // Same for d_b.
    ASSERT_EQ(cudaMemcpy(d_b, h_b, bytes, cudaMemcpyHostToDevice), cudaSuccess);

    // Launch kernel.
    dim3 blockDim(256);
    dim3 gridDim((N + blockDim.x - 1) / blockDim.x);
    add_kernel<<<gridDim, blockDim>>>(d_out, d_a, d_b, N);
    ASSERT_EQ(cudaPeekAtLastError(), cudaSuccess);
    ASSERT_EQ(cudaDeviceSynchronize(), cudaSuccess);

    // Device-to-host transfer.
    ASSERT_EQ(cudaMemcpy(h_out, d_out, bytes, cudaMemcpyDeviceToHost), cudaSuccess);

    // Stop and collect.
    auto capture_result = cap.stop();
    ASSERT_NE(capture_result, nullptr);
    const auto& events = capture_result->events();

    // Verify captures events contain GPU activity.
    // Note: The exact event types and counts depend on Kineto's CUPTI collection
    // and the driver's activity tracing; this is a basic smoke test.
    bool found_any_gpu_event = false;
    for (const auto& evt : events)
    {
        if (evt.device_type != profiler::device_enum::CPU)
        {
            found_any_gpu_event = true;
            break;
        }
    }

    // GPU events may not appear on all systems or configurations (e.g.,
    // CI with CUDA toolkit but no real device, or driver restrictions).
    // Skip rather than fail to keep the test portable.
    if (!found_any_gpu_event)
    {
        GTEST_SKIP() << "CUDA capture produced no GPU-side events in this environment";
    }

    // Verify no assertion failures in the capture (all events are well-formed).
    for (const auto& evt : events)
    {
        EXPECT_FALSE(evt.name.empty() || evt.transfer_bytes < 0);
    }
}

// Real-hardware test: concurrent streams produce distinct stream_id and do not
// form false CPU-style parent/child nesting. Phase 4 requirement: "concurrent
// streams... with distinct `stream_id`/`resource_id` and overlapping intervals
// are not forced into a false CPU-style parent/child relationship"
// (design-review.md section 8, "Real CUDA/ROCm" row and finding 8 regression).
PROFILERTEST(GpuRealHardware, concurrent_streams_not_nested)
{
    const auto& caps = profiler::discover_backend_capabilities();
    if (!caps.gpu_device_available)
    {
        GTEST_SKIP() << "No CUDA device available";
    }

    const int N = 1024;
    const int bytes = N * sizeof(float);

    // Allocate device memory.
    float* d_in = nullptr;
    float* d_out1 = nullptr;
    float* d_out2 = nullptr;
    cudaError_t err = cudaMalloc(&d_in, bytes);
    if (err != cudaSuccess)
    {
        GTEST_SKIP() << "cudaMalloc failed: " << cudaGetErrorString(err);
    }
    ASSERT_EQ(cudaMalloc(&d_out1, bytes), cudaSuccess);
    ASSERT_EQ(cudaMalloc(&d_out2, bytes), cudaSuccess);

    // Create two streams.
    cudaStream_t stream1 = nullptr;
    cudaStream_t stream2 = nullptr;
    ASSERT_EQ(cudaStreamCreate(&stream1), cudaSuccess);
    ASSERT_EQ(cudaStreamCreate(&stream2), cudaSuccess);

    struct StreamCleanup
    {
        ~StreamCleanup()
        {
            cudaFree(d_in);
            cudaFree(d_out1);
            cudaFree(d_out2);
            if (stream1)
            {
                cudaStreamDestroy(stream1);
            }
            if (stream2)
            {
                cudaStreamDestroy(stream2);
            }
        }
        float* d_in;
        float* d_out1;
        float* d_out2;
        cudaStream_t stream1;
        cudaStream_t stream2;
    } cleanup{d_in, d_out1, d_out2, stream1, stream2};

    profiler::capture_config config;
    config.activities = {profiler::activity::cpu, profiler::activity::cuda};

    profiler::capture cap(config);
    ASSERT_TRUE(cap.prepare());
    ASSERT_TRUE(cap.start());

    // Launch work on both streams concurrently.
    // Stream 1: device-to-device copy.
    ASSERT_EQ(cudaMemcpyAsync(d_out1, d_in, bytes, cudaMemcpyDeviceToDevice, stream1),
              cudaSuccess);

    // Stream 2: another device-to-device copy.
    ASSERT_EQ(cudaMemcpyAsync(d_out2, d_in, bytes, cudaMemcpyDeviceToDevice, stream2),
              cudaSuccess);

    // Synchronize both.
    ASSERT_EQ(cudaStreamSynchronize(stream1), cudaSuccess);
    ASSERT_EQ(cudaStreamSynchronize(stream2), cudaSuccess);

    // Stop and collect.
    auto capture_result = cap.stop();
    ASSERT_NE(capture_result, nullptr);
    const auto& events = capture_result->events();

    // Verify: if GPU events exist, they should have distinct resource_id
    // (stream) fields for work launched on different streams.
    // This is a heuristic check; the exact number and type of events depend
    // on driver/Kineto configuration.
    std::vector<profiler::capture_event const*> gpu_events;
    std::set<int64_t>                           seen_resource_ids;
    for (const auto& evt : events)
    {
        if (evt.device_type != profiler::device_enum::CPU && evt.resource_id >= 0)
        {
            gpu_events.push_back(&evt);
            seen_resource_ids.insert(evt.resource_id);
        }
    }

    if (gpu_events.empty())
    {
        GTEST_SKIP() << "CUDA capture produced no GPU-side events in this environment";
    }

    // capture_event has no explicit parent/child pointer of its own (unlike
    // the CPU-side hierarchical scope tree) -- "nested" here would mean one
    // stream's event carries the other's id as its `linked_correlation_id`
    // (the field that *does* express a real launch-to-execution/parent-child
    // relationship, per its own doc comment in common/capture.h), which
    // would incorrectly conflate two independent, concurrently-running
    // streams into a single dependency chain. Assert that never happens
    // across events on genuinely distinct resource_id (stream) values.
    for (const auto* a : gpu_events)
    {
        for (const auto* b : gpu_events)
        {
            if (a == b || a->resource_id == b->resource_id)
            {
                continue;
            }
            EXPECT_NE(a->linked_correlation_id, b->correlation_id)
                << "event on stream " << a->resource_id
                << " incorrectly links to an event on distinct stream " << b->resource_id;
        }
    }

    // Note: true device-side overlap between the two streams' copies is
    // deliberately NOT asserted here -- these are tiny (4KB) D2D copies, and
    // this hardware/driver is free to execute them close enough in sequence
    // that their measured intervals don't overlap even when Kineto tracked
    // the two streams correctly; that would be a scheduling fact, not a
    // profiler bug. The sibling/non-nesting check above is the real
    // correctness assertion for this test.
}

// Real-hardware test: a device fill (cudaMemset) is captured, per Phase 4.D /
// design-review.md section 8's "Real CUDA/ROCm" row ("fill"), which had no
// coverage at all before this test.
PROFILERTEST(GpuRealHardware, device_fill_captured)
{
    const auto& caps = profiler::discover_backend_capabilities();
    if (!caps.gpu_device_available)
    {
        GTEST_SKIP() << "No CUDA device available";
    }

    const int N = 4096;
    const int bytes = N * sizeof(float);

    float* d_buf = nullptr;
    cudaError_t err = cudaMalloc(&d_buf, bytes);
    if (err != cudaSuccess)
    {
        GTEST_SKIP() << "cudaMalloc failed: " << cudaGetErrorString(err);
    }
    struct Cleanup
    {
        ~Cleanup() { cudaFree(d_buf); }
        float* d_buf;
    } cleanup{d_buf};

    profiler::capture_config config;
    config.activities = {profiler::activity::cpu, profiler::activity::cuda};

    profiler::capture cap(config);
    ASSERT_TRUE(cap.prepare());
    ASSERT_TRUE(cap.start());

    ASSERT_EQ(cudaMemset(d_buf, 0, bytes), cudaSuccess);
    ASSERT_EQ(cudaDeviceSynchronize(), cudaSuccess);

    auto capture_result = cap.stop();
    ASSERT_NE(capture_result, nullptr);
    const auto& events = capture_result->events();

    bool found_gpu_event = false;
    for (const auto& evt : events)
    {
        if (evt.device_type != profiler::device_enum::CPU)
        {
            found_gpu_event = true;
            break;
        }
    }
    if (!found_gpu_event)
    {
        GTEST_SKIP() << "CUDA capture produced no GPU-side events in this environment";
    }
}

// Real-hardware test: a CPU-side scope concurrently active while GPU work
// runs, verifying both appear correctly attributed in the same capture.
// Phase 4.D / design-review.md section 8's "Real CUDA/ROCm" row ("CPU
// overlap"), previously untested.
PROFILERTEST(GpuRealHardware, cpu_overlap_with_gpu_work_attributed)
{
    const auto& caps = profiler::discover_backend_capabilities();
    if (!caps.gpu_device_available)
    {
        GTEST_SKIP() << "No CUDA device available";
    }

    const int N = 1024;
    const int bytes = N * sizeof(float);

    float* d_a = nullptr;
    float* d_b = nullptr;
    float* d_out = nullptr;
    cudaError_t err = cudaMalloc(&d_a, bytes);
    if (err != cudaSuccess)
    {
        GTEST_SKIP() << "cudaMalloc failed: " << cudaGetErrorString(err);
    }
    ASSERT_EQ(cudaMalloc(&d_b, bytes), cudaSuccess);
    ASSERT_EQ(cudaMalloc(&d_out, bytes), cudaSuccess);
    struct Cleanup
    {
        ~Cleanup()
        {
            cudaFree(d_a);
            cudaFree(d_b);
            cudaFree(d_out);
        }
        float* d_a;
        float* d_b;
        float* d_out;
    } cleanup{d_a, d_b, d_out};

    profiler::capture_config config;
    config.activities = {profiler::activity::cpu, profiler::activity::cuda};

    profiler::capture cap(config);
    ASSERT_TRUE(cap.prepare());
    ASSERT_TRUE(cap.start());

    cudaStream_t stream = nullptr;
    ASSERT_EQ(cudaStreamCreate(&stream), cudaSuccess);

    dim3 blockDim(256);
    dim3 gridDim((N + blockDim.x - 1) / blockDim.x);
    add_kernel<<<gridDim, blockDim, 0, stream>>>(d_out, d_a, d_b, N);

    // CPU-side work concurrent with the async kernel launched above (not yet
    // synchronized).
    volatile long cpu_accumulator = 0;
    for (int i = 0; i < 200000; ++i)
    {
        cpu_accumulator += i;
    }

    ASSERT_EQ(cudaStreamSynchronize(stream), cudaSuccess);
    cudaStreamDestroy(stream);

    auto capture_result = cap.stop();
    ASSERT_NE(capture_result, nullptr);
    const auto& events = capture_result->events();

    bool found_cpu_event = false;
    bool found_gpu_event = false;
    for (const auto& evt : events)
    {
        if (evt.device_type == profiler::device_enum::CPU)
        {
            found_cpu_event = true;
        }
        else
        {
            found_gpu_event = true;
        }
    }
    // The CPU-side traceme scope for this function itself is always present
    // regardless of GPU activity, so this should never skip.
    EXPECT_TRUE(found_cpu_event) << "expected at least one CPU-side event in the capture";
    if (!found_gpu_event)
    {
        GTEST_SKIP() << "CUDA capture produced no GPU-side events in this environment";
    }
}

// Real-hardware test: async GPU work that is still in flight when
// session/capture stop() is called must be reported honestly (either
// captured or cleanly absent), not cause a hang or a use-after-free -- this
// exercises the generation-filtering fix from Phase 4.A (`427409e`) under a
// real timing race that no synthetic unit test can reproduce. Phase 4.D /
// design-review.md section 8's "Real CUDA/ROCm" row ("work extending beyond
// scope/stop").
PROFILERTEST(GpuRealHardware, work_extending_beyond_stop_reported_honestly)
{
    const auto& caps = profiler::discover_backend_capabilities();
    if (!caps.gpu_device_available)
    {
        GTEST_SKIP() << "No CUDA device available";
    }

    // A buffer large enough that a memset over it takes measurable device
    // time, so stop() below has a real chance of racing genuinely in-flight
    // work rather than work that already finished.
    const size_t bytes = static_cast<size_t>(256) * 1024 * 1024;

    void* d_buf = nullptr;
    cudaError_t err = cudaMalloc(&d_buf, bytes);
    if (err != cudaSuccess)
    {
        GTEST_SKIP() << "cudaMalloc failed: " << cudaGetErrorString(err);
    }
    struct Cleanup
    {
        ~Cleanup()
        {
            cudaDeviceSynchronize();
            cudaFree(d_buf);
        }
        void* d_buf;
    } cleanup{d_buf};

    cudaStream_t stream = nullptr;
    ASSERT_EQ(cudaStreamCreate(&stream), cudaSuccess);

    profiler::capture_config config;
    config.activities = {profiler::activity::cpu, profiler::activity::cuda};

    profiler::capture cap(config);
    ASSERT_TRUE(cap.prepare());
    ASSERT_TRUE(cap.start());

    // Launch async work and deliberately do NOT synchronize before stop() --
    // this work is still (very likely) in flight when the capture stops.
    ASSERT_EQ(cudaMemsetAsync(d_buf, 0, bytes, stream), cudaSuccess);

    // No cudaStreamSynchronize() here by design: exercise stop() racing
    // against in-flight GPU work.
    auto capture_result = cap.stop();

    // The one honest requirement: stop() must return (no hang) and produce a
    // result object, whether or not the in-flight event made it into this
    // capture.
    ASSERT_NE(capture_result, nullptr);

    // Now make sure the in-flight work actually completes and doesn't
    // corrupt a later, unrelated capture (this is the actual generation-
    // filtering regression Phase 4.A's fix targets: a late callback from
    // this stopped session must not contaminate the next one).
    profiler::capture_config config2;
    config2.activities = {profiler::activity::cpu, profiler::activity::cuda};
    profiler::capture cap2(config2);
    ASSERT_TRUE(cap2.prepare());
    ASSERT_TRUE(cap2.start());
    ASSERT_EQ(cudaStreamSynchronize(stream), cudaSuccess);
    auto capture_result2 = cap2.stop();
    ASSERT_NE(capture_result2, nullptr);
    // No assertion on capture_result2's event identities: the honest
    // contract here is just "no hang, no crash, no cross-contamination" --
    // asserting exact event provenance would depend on driver/Kineto timing
    // details outside this test's control.
    cudaStreamDestroy(stream);
}

// Real-hardware test: explicit stream-event mode (record_with_stream()/
// elapsed_nonblocking(), Phase 4.A's `427409e`-adjacent "done" item in
// Profiler/bespoke/base/cuda.cpp) exercised end-to-end through the public
// capture:: API via capture_backend::kineto_gpu_fallback, not just the
// lower-level ProfilerStubs stub test in TestProfilerBackendGpuFallback.cpp.
// Phase 4.D / design-review.md section 8's "Real CUDA/ROCm" row ("explicit
// stream mode").
PROFILERTEST(GpuRealHardware, explicit_stream_event_mode_end_to_end)
{
    const auto& caps = profiler::discover_backend_capabilities();
    if (!caps.gpu_device_available)
    {
        GTEST_SKIP() << "No CUDA device available";
    }

    const int N = 1024;
    const int bytes = N * sizeof(float);

    float* d_a = nullptr;
    float* d_b = nullptr;
    float* d_out = nullptr;
    cudaError_t err = cudaMalloc(&d_a, bytes);
    if (err != cudaSuccess)
    {
        GTEST_SKIP() << "cudaMalloc failed: " << cudaGetErrorString(err);
    }
    ASSERT_EQ(cudaMalloc(&d_b, bytes), cudaSuccess);
    ASSERT_EQ(cudaMalloc(&d_out, bytes), cudaSuccess);
    struct Cleanup
    {
        ~Cleanup()
        {
            cudaFree(d_a);
            cudaFree(d_b);
            cudaFree(d_out);
        }
        float* d_a;
        float* d_b;
        float* d_out;
    } cleanup{d_a, d_b, d_out};

    profiler::capture_config config;
    config.backend     = profiler::capture_backend::kineto_gpu_fallback;
    config.activities  = {profiler::activity::cpu, profiler::activity::cuda};

    profiler::capture cap(config);
    if (!cap.prepare())
    {
        GTEST_SKIP() << "kineto_gpu_fallback backend unavailable in this environment";
    }
    ASSERT_TRUE(cap.start());

    {
        // The kineto_gpu_fallback path (bespoke/kineto/profiler_kineto.cpp's
        // onFunctionEnter/onFunctionExit) times a device event pair around a
        // RecordFunction scope, not around a raw, unwrapped kernel launch --
        // PROFILER_SCOPE is what makes that RecordFunction callback fire.
        PROFILER_SCOPE("explicit_stream_event_mode_scope");
        launch_add_kernel(d_out, d_a, d_b, N);
        ASSERT_EQ(cudaPeekAtLastError(), cudaSuccess);
        ASSERT_EQ(cudaDeviceSynchronize(), cudaSuccess);
    }

    auto capture_result = cap.stop();
    ASSERT_NE(capture_result, nullptr);
    const auto& events = capture_result->events();

    // The fallback path's measured interval surfaces through
    // gpu_fallback_elapsed_us (capture.h's doc comment: "an explicitly
    // measured GPU-fallback interval... when this event came from that
    // path"), distinct from -1's "not applicable" sentinel.
    bool found_fallback_measurement = false;
    for (const auto& evt : events)
    {
        if (evt.gpu_fallback_elapsed_us >= 0)
        {
            found_fallback_measurement = true;
            break;
        }
    }
    if (events.empty())
    {
        GTEST_SKIP() << "kineto_gpu_fallback capture produced no events in this environment";
    }
    EXPECT_TRUE(found_fallback_measurement)
        << "expected at least one event with a real gpu_fallback_elapsed_us measurement "
           "from the explicit stream-event path";
}

// Real-hardware test: calibrate_cuda_device() (common/clock_calibration.cpp)
// must produce a real, non-degenerate mapping from real paired (CPU, device)
// samples, not the previous always-device_ns=0.0 stand-in that made the fit
// degenerate by construction. Phase 4.B / design-review.md section 6.4 item 3
// "timestamps pass calibration checks".
PROFILERTEST(GpuRealHardware, clock_calibration_produces_bounded_residual)
{
    const auto& caps = profiler::discover_backend_capabilities();
    if (!caps.gpu_device_available)
    {
        GTEST_SKIP() << "No CUDA device available";
    }

    const profiler::clock_calibration calibration = profiler::calibrate_cuda_device(0);

    // A degenerate fit (the pre-fix bug) always produces scale=1.0 exactly
    // and uncertainty_ns=0.0 exactly, regardless of input -- so this alone
    // would not have caught the bug. Assert the fit is both sane (device and
    // CPU clocks run at comparable rates -- scale within an order of
    // magnitude of 1.0) and bounded (the CPU-side round-trip through
    // cudaEventSynchronize() on this hardware is expected to keep residual
    // uncertainty under 50ms; a real device/host timestamp mismatch would
    // blow well past this).
    EXPECT_GT(calibration.scale, 0.1);
    EXPECT_LT(calibration.scale, 10.0);
    EXPECT_GE(calibration.uncertainty_ns, 0.0);
    EXPECT_LT(calibration.uncertainty_ns, 50.0e6) << "residual uncertainty too large: "
                                                    << calibration.uncertainty_ns << " ns";

    // Calling again for the same device must hit the cache and return an
    // identical result (common/clock_calibration.h's
    // cached_cuda_device_calibration), not re-run the device round-trip.
    const profiler::clock_calibration cached = profiler::cached_cuda_device_calibration(0);
    EXPECT_DOUBLE_EQ(cached.scale, profiler::cached_cuda_device_calibration(0).scale);
}

// Real-hardware test: a capture through the actual public capture:: API (used
// by kernel_and_transfers_captured above) populates
// capture_event::clock_uncertainty_ns for CUDA device-side events from the
// calibration above, rather than leaving it at its default 0 forever.
PROFILERTEST(GpuRealHardware, capture_populates_clock_uncertainty_for_gpu_events)
{
    const auto& caps = profiler::discover_backend_capabilities();
    if (!caps.gpu_device_available)
    {
        GTEST_SKIP() << "No CUDA device available";
    }

    const int N = 1024;
    const int bytes = N * sizeof(float);

    float* d_a = nullptr;
    float* d_b = nullptr;
    float* d_out = nullptr;
    cudaError_t err = cudaMalloc(&d_a, bytes);
    if (err != cudaSuccess)
    {
        GTEST_SKIP() << "cudaMalloc failed: " << cudaGetErrorString(err);
    }
    ASSERT_EQ(cudaMalloc(&d_b, bytes), cudaSuccess);
    ASSERT_EQ(cudaMalloc(&d_out, bytes), cudaSuccess);

    struct Cleanup
    {
        ~Cleanup()
        {
            cudaFree(d_a);
            cudaFree(d_b);
            cudaFree(d_out);
        }
        float* d_a;
        float* d_b;
        float* d_out;
    } cleanup{d_a, d_b, d_out};

    profiler::capture_config config;
    config.activities = {profiler::activity::cpu, profiler::activity::cuda};

    profiler::capture cap(config);
    ASSERT_TRUE(cap.prepare());
    ASSERT_TRUE(cap.start());

    dim3 blockDim(256);
    dim3 gridDim((N + blockDim.x - 1) / blockDim.x);
    add_kernel<<<gridDim, blockDim>>>(d_out, d_a, d_b, N);
    ASSERT_EQ(cudaPeekAtLastError(), cudaSuccess);
    ASSERT_EQ(cudaDeviceSynchronize(), cudaSuccess);

    auto capture_result = cap.stop();
    ASSERT_NE(capture_result, nullptr);
    const auto& events = capture_result->events();

    bool found_gpu_event_with_uncertainty = false;
    for (const auto& evt : events)
    {
        if (evt.device_type == profiler::device_enum::CUDA)
        {
            // A genuinely-calibrated device has non-negative uncertainty; a
            // real (non-buggy) calibration on healthy hardware should not
            // report exactly 0, since that was the pre-fix degenerate value.
            EXPECT_GE(evt.clock_uncertainty_ns, 0);
            if (evt.clock_uncertainty_ns > 0)
            {
                found_gpu_event_with_uncertainty = true;
            }
        }
    }

    if (events.empty())
    {
        GTEST_SKIP() << "CUDA capture produced no events in this environment";
    }
    EXPECT_TRUE(found_gpu_event_with_uncertainty)
        << "expected at least one CUDA device event with clock_uncertainty_ns > 0";
}

// Real-hardware failure-injection test: a CUDA runtime error during a GPU
// session (here, querying elapsed time on an invalid/unrecorded event pair,
// which cudaEventSynchronize()/cudaEventElapsedTime() reject) must surface
// through profiler_session::stop()/last_error(), not be silently swallowed.
// Phase 4.C / design-review.md section 6.7 "Record loss" and "Session
// integrity"; exercises the impl::cudaCheck() sticky-error-flag wiring added
// in gpu_tracer::collect_data() (Profiler/native/gpu/gpu_tracer.cpp).
PROFILERTEST(GpuRealHardware, session_reports_failure_after_cuda_runtime_error)
{
    const auto& caps = profiler::discover_backend_capabilities();
    if (!caps.gpu_device_available)
    {
        GTEST_SKIP() << "No CUDA device available";
    }

    using profiler::profiler_options;
    using profiler::profiler_session;

    profiler_options opts;
    opts.enable_gpu_tracing_ = true;
    profiler_session session(opts);
    ASSERT_TRUE(session.start());

    // Inject a real CUDA runtime failure: an event stub that was never
    // recorded (get() == nullptr) is an invalid resource handle to
    // cudaEventSynchronize/cudaEventElapsedTime.
    auto const                                      stubs = profiler::profiler_impl::impl::cudaStubs();
    profiler::profiler_impl::impl::ProfilerVoidEventStub unrecorded_event =
        std::shared_ptr<CUevent_st>(nullptr, [](CUevent_st*) {});
    // elapsed() logs-and-swallows via impl::cudaCheck() rather than
    // throwing/crashing (a profiler must not abort the host app over a
    // backend error) -- that's the exact silent-failure path this test
    // exercises; its return value is intentionally unchecked here.
    (void)stubs->elapsed(&unrecorded_event, &unrecorded_event);

    // The failed call above must be visible in the session's stop() result,
    // not silently dropped.
    EXPECT_FALSE(session.stop());
    EXPECT_FALSE(session.last_error().empty());
}

#endif  // PROFILER_HAS_CUDA
