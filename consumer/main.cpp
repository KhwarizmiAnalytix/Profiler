#include "profiler.h"

int main()
{
    profiler::session session;
    if (!session.start())
    {
        return 1;
    }
    PROFILER_SCOPE("consumer");
    if (!session.stop())
    {
        return 1;
    }
    return session.write_trace("consumer_trace.json") ? 0 : 1;
}
