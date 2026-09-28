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

#include "bespoke/common/orchestration/python_tracer.h"

namespace profiler::profiler_impl::impl::python_tracer
{
namespace
{
MakeFn       make_fn;
MakeMemoryFn memory_make_fn;

struct NoOpPythonTracer : public PythonTracerBase
{
    NoOpPythonTracer()           = default;
    ~NoOpPythonTracer() override = default;

    void                                 stop() override {}
    void                                 restart() override {}
    void                                 register_gc_callback() override {}
    std::vector<std::shared_ptr<Result>> getEvents(
        std::function<profiler::time_t(profiler::approx_time_t)> /*time_converter*/,
        std::vector<CompressedEvent>& /*enters*/,
        profiler::time_t /*end_time_ns*/) override
    {
        return {};
    }
};

struct NoOpMemoryPythonTracer : public PythonMemoryTracerBase
{
    NoOpMemoryPythonTracer()           = default;
    ~NoOpMemoryPythonTracer() override = default;
    void start() override {}
    void stop() override {}
    void export_memory_history(const std::string& /*path*/) override {}
};

}  // namespace

void registerTracer(MakeFn make_tracer)
{
    make_fn = make_tracer;
}

std::unique_ptr<PythonTracerBase> PythonTracerBase::make(RecordQueue* queue)
{
    if (make_fn == nullptr)
    {
        return std::make_unique<NoOpPythonTracer>();
    }
    return make_fn(queue);
}

void registerMemoryTracer(MakeMemoryFn make_memory_tracer)
{
    memory_make_fn = make_memory_tracer;
}

std::unique_ptr<PythonMemoryTracerBase> PythonMemoryTracerBase::make()
{
    if (memory_make_fn == nullptr)
    {
        return std::make_unique<NoOpMemoryPythonTracer>();
    }
    return memory_make_fn();
}
}  // namespace profiler::profiler_impl::impl::python_tracer
