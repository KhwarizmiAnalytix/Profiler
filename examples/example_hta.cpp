// CPU Kineto capture with iteration and rank metadata for Holistic Trace Analysis.
// See docs/hta.md for CUDA capture requirements and offline analysis.
#include <cmath>
#include <filesystem>
#include <iostream>
#include <string>

#include "profiler.h"

namespace
{
double compute()
{
    PROFILER_OP("compute");
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

    profiler::session_options options;
    options.backend    = profiler::capture_backend::kineto;
    options.activities = {profiler::activity::cpu};
    profiler::session session(options);
    if (!session.start())
    {
        std::cerr << "Could not start a profiling session\n";
        return 1;
    }
    profiler::add_metadata_json("distributedInfo", R"({"rank": 0, "world_size": 1})");

    double checksum = 0.0;
    for (int i = 0; i < 4; ++i)
    {
        const std::string step = "ProfilerStep#" + std::to_string(i);
        PROFILER_SCOPE(step);
        checksum += compute();
    }

    if (!session.stop())
    {
        std::cerr << "Could not stop the profiling session\n";
        return 1;
    }
    const auto trace_path = output / "rank0.json";
    if (!session.write_trace(trace_path.string()))
    {
        std::cerr << "Could not write a Kineto trace\n";
        return 1;
    }
    std::cout << "checksum=" << checksum << '\n';
    std::cout << "Wrote " << trace_path.string() << '\n';
    std::cout
        << "CPU-only capture: inspect operators with HTA; GPU analyses require CUDA activities.\n";
    return 0;
}
