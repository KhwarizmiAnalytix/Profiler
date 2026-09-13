#include "profiler.h"

int main()
{
    profiler::profiler_session session;
    if (!session.start())
    {
        return 1;
    }
    PROFILER_PROFILE_SCOPE("consumer");
    if (!session.stop())
    {
        return 1;
    }
    return session.write_chrome_trace("consumer_trace.json") ? 0 : 1;
}
