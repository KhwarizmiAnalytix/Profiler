#include "common/instrumentation.h"
#include "native/session/profiler.h"

int main()
{
    profiler::profiler_options opts;
    profiler::profiler_session session(opts);
    PROFILER_PROFILE_SCOPE("consumer");
    return 0;
}
