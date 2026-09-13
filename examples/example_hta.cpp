// CPU Kineto capture with iteration and rank metadata for Holistic Trace Analysis.
// See docs/hta.md for CUDA capture requirements and offline analysis.
#include <cmath>
#include <filesystem>
#include <iostream>
#include <string>

#include "profiler.h"
#include "bespoke/kineto/kineto_shim.h"
#include "bespoke/kineto/profiler_kineto.h"

namespace
{
double compute()
{
    PROFILER_RECORD_FUNCTION("compute");
    double sum = 0.0;
    for (int i = 0; i < 20000; ++i)
    {
        sum += std::sin(static_cast<double>(i) * 0.001);
    }
    return sum;
}
}  // namespace

int main(int argc, char** argv)
{
    const std::filesystem::path output = argc > 1 ? argv[1] : "traces/hta";
    std::filesystem::create_directories(output);
    using namespace profiler::profiler_impl;
    const ProfilerConfig config(ProfilerState::KINETO);
    const std::set<ActivityType> activities{ActivityType::CPU};

    prepareProfiler(config, activities);
    enableProfiler(config, activities);
    addMetadataJson("distributedInfo", R"({"rank": 0, "world_size": 1})");

    double checksum = 0.0;
    for (int i = 0; i < 4; ++i)
    {
        const std::string step = "ProfilerStep#" + std::to_string(i);
        PROFILER_RECORD_USER_SCOPE(step);
        checksum += compute();
    }

    auto result = disableProfiler();
    const auto trace_path = output / "rank0.json";
    if (!result || !result->save(trace_path.string()))
    {
        std::cerr << "Could not write a Kineto trace\n";
        return 1;
    }
    std::cout << "checksum=" << checksum << '\n';
    std::cout << "Wrote " << trace_path.string() << '\n';
    std::cout << "CPU-only capture: inspect operators with HTA; GPU analyses require CUDA activities.\n";
    return 0;
}
