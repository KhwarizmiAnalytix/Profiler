/*
 * XSigma ITT API Wrapper Implementation
 *
 * Provides C++ wrapper functions for Intel ITT API, aligned with XSigma's
 * implementation for feature parity.
 */

#include "itt_wrapper.h"

#if PROFILER_HAS_ITT
#include <ittnotify.h>

#include <mutex>
#include <unordered_map>
#endif

namespace profiler
{
namespace profiler_impl
{

#if PROFILER_HAS_ITT

namespace
{
// Global ITT domain for Profiler
__itt_domain* g_itt_domain = nullptr;
std::mutex    g_itt_init_mutex;

// Thread-local string handles cache
thread_local std::unordered_map<std::string, __itt_string_handle*> g_string_handles;
}  // namespace

void itt_init()
{
    std::scoped_lock const lock(g_itt_init_mutex);

    if (g_itt_domain == nullptr)
    {
        g_itt_domain = __itt_domain_create("Profiler");
    }
}

void itt_range_push(const char* name)
{
    if (g_itt_domain == nullptr)
    {
        itt_init();
    }

    if (g_itt_domain != nullptr && name != nullptr)
    {
        __itt_string_handle* handle = __itt_string_handle_create(name);
        __itt_task_begin(g_itt_domain, __itt_null, __itt_null, handle);
    }
}

void itt_range_pop()
{
    if (g_itt_domain != nullptr)
    {
        __itt_task_end(g_itt_domain);
    }
}

void itt_mark(const char* name)
{
    if (g_itt_domain == nullptr)
    {
        itt_init();
    }

    if (g_itt_domain != nullptr && name != nullptr)
    {
        __itt_string_handle* handle = __itt_string_handle_create(name);
        __itt_task_begin(g_itt_domain, __itt_null, __itt_null, handle);
        __itt_task_end(g_itt_domain);
    }
}

__itt_domain* itt_get_domain()
{
    if (g_itt_domain == nullptr)
    {
        itt_init();
    }
    return g_itt_domain;
}

#endif  // PROFILER_HAS_ITT

}  // namespace profiler_impl
}  // namespace profiler
