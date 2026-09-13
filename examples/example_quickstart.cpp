/**
 * Minimal standalone app: profile a loop and write a Chrome / Perfetto trace.
 * This is the integration shape any other repository should copy.
 */

#include <cmath>
#include <iostream>

#include "profiler.h"

namespace
{

double busy_work(int n)
{
    PROFILER_PROFILE_FUNCTION();
    double acc = 0.0;
    for (int i = 0; i < n; ++i)
    {
        acc += std::sin(static_cast<double>(i) * 0.001);
    }
    return acc;
}

}  // namespace

int main()
{
    profiler::profiler_session session;
    if (!session.start())
    {
        std::cerr << "profiler_session::start failed\n";
        return 1;
    }

    double total = 0.0;
    {
        PROFILER_PROFILE_SCOPE("workload");
        for (int pass = 0; pass < 4; ++pass)
        {
            PROFILER_PROFILE_SCOPE("pass");
            total += busy_work(20000);
        }
    }

    if (!session.stop())
    {
        std::cerr << "profiler_session::stop failed\n";
        return 1;
    }

    const char* path = "quickstart_trace.json";
    if (!session.write_chrome_trace(path))
    {
        std::cerr << "write_chrome_trace failed\n";
        return 1;
    }

    std::cout << "checksum=" << total << "\n";
    std::cout << "Wrote " << path << " — open in chrome://tracing or https://ui.perfetto.dev\n";
    return 0;
}
