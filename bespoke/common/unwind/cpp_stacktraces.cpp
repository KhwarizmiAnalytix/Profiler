#include "bespoke/common/unwind/cpp_stacktraces.h"

#include <cstdlib>
#include <cstring>

#include "util/env.h"

namespace profiler
{
namespace
{
bool compute_cpp_stack_traces_enabled()
{
    return profiler::utils::check_env("PROFILER_SHOW_CPP_STACKTRACES") == true;
}

bool compute_disable_addr2line()
{
    return profiler::utils::check_env("PROFILER_DISABLE_ADDR2LINE") == true;
}
}  // namespace

bool get_cpp_stacktraces_enabled()
{
    static bool const enabled = compute_cpp_stack_traces_enabled();
    return enabled;
}

static profiler::unwind::Mode compute_symbolize_mode()
{
    auto envar_c = profiler::utils::get_env("PROFILER_SYMBOLIZE_MODE");
    if (envar_c.has_value())
    {
        if (envar_c == "dladdr")
        {
            return unwind::Mode::dladdr;
        }
        if (envar_c == "addr2line")
        {
            return unwind::Mode::addr2line;
        }
        if (envar_c == "fast")
        {
            return unwind::Mode::fast;
        }

        // PROFILER_CHECK(
        // false,
        // "expected {{dladdr, addr2line, fast}} for PROFILER_SYMBOLIZE_MODE, got {}",
        // envar_c.value());
        // Unreachable: PROFILER_CHECK will throw/abort on failure
        return unwind::Mode::dladdr;
    }

    return compute_disable_addr2line() ? unwind::Mode::dladdr : unwind::Mode::addr2line;
}

unwind::Mode get_symbolize_mode()
{
    static unwind::Mode const mode = compute_symbolize_mode();
    return mode;
}

}  // namespace profiler
