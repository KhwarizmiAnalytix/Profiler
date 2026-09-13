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

#import <Foundation/Foundation.h>
#import <Metal/Metal.h>

#include "native/gpu/gpu_tracer.h"

#include <cstdint>
#include <string>
#include <string_view>
#include <utility>

#include "native/tracing/traceme.h"

namespace profiler::profiler_impl
{
namespace
{

constexpr const char* kProbeKernelSource = R"METAL(
#include <metal_stdlib>
using namespace metal;

kernel void profiler_metal_probe(device float* buf [[buffer(0)]],
                                 uint tid [[thread_position_in_grid]])
{
    buf[tid] = buf[tid] * 1.0001f + 0.001f;
}
)METAL";

uint64_t seconds_to_ns(CFTimeInterval seconds)
{
    if (seconds <= 0.0)
    {
        return 0;
    }
    return static_cast<uint64_t>(seconds * 1e9);
}

}  // namespace

bool run_gpu_kernel_probe(std::string_view kernel_name)
{
    id<MTLDevice> device = MTLCreateSystemDefaultDevice();
    if (device == nil)
    {
        return false;
    }

    NSError*       err = nil;
    NSString*      src = [NSString stringWithUTF8String:kProbeKernelSource];
    id<MTLLibrary> lib = [device newLibraryWithSource:src options:nil error:&err];
    if (lib == nil)
    {
        return false;
    }

    id<MTLFunction> fn = [lib newFunctionWithName:@"profiler_metal_probe"];
    if (fn == nil)
    {
        return false;
    }

    id<MTLComputePipelineState> pso = [device newComputePipelineStateWithFunction:fn error:&err];
    if (pso == nil)
    {
        return false;
    }

    id<MTLCommandQueue> queue = [device newCommandQueue];
    if (queue == nil)
    {
        return false;
    }

    constexpr NSUInteger kElems = 1 << 20;
    id<MTLBuffer>        buf    = [device newBufferWithLength:(kElems * sizeof(float))
                                            options:MTLResourceStorageModeShared];
    if (buf == nil)
    {
        return false;
    }

    id<MTLCommandBuffer>         cb  = [queue commandBuffer];
    id<MTLComputeCommandEncoder> enc = [cb computeCommandEncoder];
    [enc setComputePipelineState:pso];
    [enc setBuffer:buf offset:0 atIndex:0];
    NSUInteger tg = pso.maxTotalThreadsPerThreadgroup;
    if (tg == 0)
    {
        tg = 1;
    }
    if (tg > kElems)
    {
        tg = kElems;
    }
    [enc dispatchThreads:MTLSizeMake(kElems, 1, 1) threadsPerThreadgroup:MTLSizeMake(tg, 1, 1)];
    [enc endEncoding];

    const uint64_t host_start_ns = static_cast<uint64_t>(get_current_time_nanos());
    [cb commit];
    [cb waitUntilCompleted];
    const uint64_t host_end_ns = static_cast<uint64_t>(get_current_time_nanos());

    if (cb.status == MTLCommandBufferStatusError)
    {
        return false;
    }

    uint64_t start_ns = host_start_ns;
    uint64_t end_ns   = host_end_ns > host_start_ns ? host_end_ns : host_start_ns + 1;
    if (@available(macOS 10.15, *))
    {
        const uint64_t gpu_start_ns = seconds_to_ns(cb.GPUStartTime);
        const uint64_t gpu_end_ns   = seconds_to_ns(cb.GPUEndTime);
        if (gpu_end_ns > gpu_start_ns)
        {
            start_ns = gpu_start_ns;
            end_ns   = gpu_end_ns;
        }
    }

    const std::string name = kernel_name.empty() ? "profiler_gpu_probe" : std::string(kernel_name);
    gpu_tracer_event  event;
    event.type          = gpu_tracer_event_type::kernel;
    event.name          = name;
    event.start_time_ns = start_ns;
    event.end_time_ns   = end_ns;
    add_gpu_tracer_event(std::move(event));
    return true;
}

}  // namespace profiler::profiler_impl
