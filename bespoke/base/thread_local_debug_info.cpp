#include "bespoke/base/thread_local_debug_info.h"

#include <utility>

#include "common/profiler_export.h"

namespace profiler
{

static thread_local std::shared_ptr<thread_local_debug_info> tls_debug_info;
#define debug_info (tls_debug_info)

/* static */
DebugInfoBase* thread_local_debug_info::get(DebugInfoKind kind)
{
    thread_local_debug_info const* cur = debug_info.get();
    while (cur != nullptr)
    {
        if (cur->kind_ == kind)
        {
            return cur->info_.get();
        }
        cur = cur->parent_info_.get();
    }
    return nullptr;
}

/* static */
std::shared_ptr<thread_local_debug_info> thread_local_debug_info::current()
{
    return debug_info;
}

/* static */
void thread_local_debug_info::_forceCurrentDebugInfo(std::shared_ptr<thread_local_debug_info> info)
{
    debug_info = std::move(info);
}

/* static */
void thread_local_debug_info::_push(DebugInfoKind kind, std::shared_ptr<DebugInfoBase> info)
{
    auto prev_info           = debug_info;
    debug_info               = std::make_shared<thread_local_debug_info>();
    debug_info->parent_info_ = prev_info;
    debug_info->kind_        = kind;
    debug_info->info_        = std::move(info);
}

/* static */
std::shared_ptr<DebugInfoBase> thread_local_debug_info::_pop(DebugInfoKind /*kind*/)
{
    //PROFILER_CHECK(//NOLINT
    //    debug_info!=nullptr && debug_info->kind_ == kind, "Expected debug info of type ", (size_t)kind);//NOLINT
    auto res   = debug_info;
    debug_info = debug_info->parent_info_;
    return res->info_;
}

/* static */
std::shared_ptr<DebugInfoBase> thread_local_debug_info::_peek(DebugInfoKind /*kind*/)
{
    //PROFILER_CHECK(
    //    debug_info!=nullptr && debug_info->kind_ == kind, "Expected debug info of type ", (size_t)kind);
    return debug_info->info_;
}

DebugInfoGuard::DebugInfoGuard(DebugInfoKind kind, std::shared_ptr<DebugInfoBase> info)
{
    if (!info)
    {
        return;
    }
    prev_info_ = debug_info;
    thread_local_debug_info::_push(kind, std::move(info));
    active_ = true;
}

DebugInfoGuard::~DebugInfoGuard()
{
    if (active_)
    {
        debug_info = prev_info_;
    }
}

// Used only for setting a debug info after crossing the thread boundary;
// in this case we assume that thread pool's thread does not have an
// active debug info
DebugInfoGuard::DebugInfoGuard(std::shared_ptr<thread_local_debug_info> info)
{
    if (!info)
    {
        return;
    }
    prev_info_ = std::move(debug_info);
    debug_info = std::move(info);
    active_    = true;
}

}  // namespace profiler
