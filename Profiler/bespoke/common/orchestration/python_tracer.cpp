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
