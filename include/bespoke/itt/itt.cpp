#include "bespoke/base/base.h"
#include "bespoke/itt/itt_wrapper.h"
#include "common/irange.h"

namespace profiler::profiler_impl::impl
{
namespace
{

struct ITTMethods : public ProfilerStubs
{
    void record(int16_t* device, ProfilerVoidEventStub* event, int64_t* cpu_ns) const override {}

    float elapsed(const ProfilerVoidEventStub* /*event*/, const ProfilerVoidEventStub* /*event2*/)
        const override
    {
        return 0;
    }

    void mark(const char* name) const override { profiler::profiler_impl::itt_mark(name); }

    void rangePush(const char* name) const override
    {
        profiler::profiler_impl::itt_range_push(name);
    }

    void rangePop() const override { profiler::profiler_impl::itt_range_pop(); }

    void onEachDevice(std::function<void(int)> op) const override {}

    void synchronize() const override {}

    [[nodiscard]] bool enabled() const override { return true; }
};

struct RegisterITTMethods
{
    RegisterITTMethods()
    {
        static ITTMethods methods;
        registerITTMethods(&methods);
    }
};
RegisterITTMethods reg;

}  // namespace
}  // namespace profiler::profiler_impl::impl
