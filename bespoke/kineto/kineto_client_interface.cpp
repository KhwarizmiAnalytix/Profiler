#include "bespoke/kineto/kineto_client_interface.h"

#if PROFILER_HAS_KINETO
//#include <Profiler/Context.h>

#include <libkineto.h>

#include <chrono>
#include <cstdlib>
#include <thread>

#include "bespoke/kineto/profiler_kineto.h"
//#include "util/env.h"

// Ondemand tracing is not supported on Apple or edge platform
#if defined(__APPLE__) || defined(EDGE_PROFILER_USE_KINETO)
#define ENABLE_GLOBAL_OBSERVER (0)
#else
#define ENABLE_GLOBAL_OBSERVER (1)
#endif

namespace profiler
{

namespace profiler_impl::impl
{

namespace
{

using namespace profiler::profiler_impl;

class LibKinetoClient : public libkineto::ClientInterface
{
public:
    void init() override { /* XSigma has no MTIA backend */ }

    void prepare(
        bool report_input_shapes = false,
        bool profile_memory      = false,
        bool with_stack          = false,
        bool with_flops          = false,
        bool with_modules        = false) override
    {
        reportInputShapes_ = report_input_shapes;
        profileMemory_     = profile_memory;
        withStack_         = with_stack;
        withFlops_         = with_flops;
        withModules_       = with_modules;
    }

    void start() override
    {
        ProfilerConfig const cfg{
            ProfilerState::KINETO_ONDEMAND,
            /*report_input_shapes=*/reportInputShapes_,
            /*profile_memory=*/profileMemory_,
            /*with_stack=*/withStack_,
            /*with_flops=*/withFlops_,
            /*with_modules=*/withModules_};
        std::set<ActivityType> const              activities{ActivityType::CPU};
        std::unordered_set<profiler::RecordScope> scopes;
        scopes.insert(profiler::RecordScope::FUNCTION);
        scopes.insert(profiler::RecordScope::USER_SCOPE);
        scopes.insert(profiler::RecordScope::BACKWARD_FUNCTION);
        enableProfiler(cfg, activities, scopes);
    }

    void stop() override { (void)disableProfiler(); }

    void start_memory_profile() override { startMemoryProfile(); }

    void stop_memory_profile() override { stopMemoryProfile(); }

    void export_memory_profile(const std::string& path) override { exportMemoryProfile(path); }

private:
    // Temporarily disable shape collection until
    // we re-roll out the feature for on-demand cases
    bool reportInputShapes_{false};
    bool profileMemory_{false};
    bool withStack_{false};
    bool withFlops_{false};
    bool withModules_{false};
};

}  // namespace

}  // namespace profiler_impl::impl

void global_kineto_init()
{
#if ENABLE_GLOBAL_OBSERVER
    const char* const envar = std::getenv("KINETO_USE_DAEMON");
    if (envar != nullptr)
    {
        libkineto_init(
            /*cpuOnly=*/!(profiler::hasGPU()),
            /*logOnError=*/true);
        libkineto::api().suppressLogMessages();
    }
#endif
}

#if ENABLE_GLOBAL_OBSERVER
namespace
{

struct RegisterLibKinetoClient
{
    RegisterLibKinetoClient()
    {
        static profiler_impl::impl::LibKinetoClient client;
        libkineto::api().registerClient(&client);
    }
} register_libkineto_client;

}  // namespace
#endif

}  // namespace profiler

#else  // !PROFILER_HAS_KINETO

namespace profiler
{

void global_kineto_init() {}

}  // namespace profiler

#endif  // PROFILER_HAS_KINETO
