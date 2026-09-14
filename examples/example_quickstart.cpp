/**
 * Minimal standalone app: one session, one set of macros, one trace file.
 * Native collection and the compiled Kineto/ITT backend run together.
 */

#include <cmath>
#include <iostream>

#include "profiler.h"

namespace
{

double busy_work(int n)
{
    PROFILER_FUNCTION();
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
    profiler::session session;
    if (!session.start())
    {
        std::cerr << "profiler::session::start failed\n";
        return 1;
    }

    double total = 0.0;
    {
        PROFILER_SCOPE("workload");
        for (int pass = 0; pass < 4; ++pass)
        {
            PROFILER_SCOPE("pass");
            total += busy_work(20000);
        }
    }

    if (!session.stop())
    {
        std::cerr << "profiler::session::stop failed\n";
        return 1;
    }

    const char* path = "quickstart_trace.json";
    if (!session.write_trace(path))
    {
        std::cerr << "write_trace failed\n";
        return 1;
    }

    std::cout << "checksum=" << total << "\n";
    std::cout << "Wrote " << path << " — open in chrome://tracing or https://ui.perfetto.dev\n";
    return 0;
}
