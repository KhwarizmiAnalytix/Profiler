#include "bespoke/common/api.h"

namespace profiler::profiler_impl::impl
{

void pushITTCallbacks(
    const ProfilerConfig& config, const std::unordered_set<profiler::RecordScope>& scopes);

}  // namespace profiler::profiler_impl::impl
