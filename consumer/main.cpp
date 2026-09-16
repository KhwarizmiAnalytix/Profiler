#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>

#include "profiler.h"

int main()
{
    profiler::session session;
    if (!session.start())
    {
        return 1;
    }
    { PROFILER_SCOPE("consumer_registration_probe"); }
    if (!session.stop())
    {
        return 1;
    }

    // A passing write_trace() alone doesn't prove any collector actually ran --
    // this repo's self-registering GPU/host/python tracer and CUDA/ITT stub
    // translation units register purely via file-scope static initializers
    // that nothing else in the library references, so an ordinary static-
    // archive link can silently drop them (see cmake/ProfilerLinking.cmake).
    // Checking the exported trace actually contains this scope's name proves
    // the host tracer this consumer links against genuinely registered and
    // recorded something, not just that the API compiled/linked/returned true.
    const char* const path = "consumer_trace.json";
    if (!session.write_trace(path))
    {
        return 1;
    }

    std::ifstream file(path);
    if (!file.good())
    {
        return 1;
    }
    std::ostringstream contents;
    contents << file.rdbuf();
    if (contents.str().find("consumer_registration_probe") == std::string::npos)
    {
        std::fprintf(stderr, "consumer_trace.json is missing the recorded scope\n");
        return 1;
    }
    return 0;
}
