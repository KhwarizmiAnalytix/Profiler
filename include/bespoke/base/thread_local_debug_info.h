#pragma once

#include <cstdint>
#include <memory>

#include "common/profiler_export.h"

namespace profiler
{

enum class PROFILER_VISIBILITY_ENUM DebugInfoKind : uint8_t
{
    PRODUCER_INFO = 0,
    MOBILE_RUNTIME_INFO,
    PROFILER_STATE,
    INFERENCE_CONTEXT,  // for inference usage
    PARAM_COMMS_INFO,

    TEST_INFO,    // used only in tests
    TEST_INFO_2,  // used only in tests
};

class PROFILER_API DebugInfoBase
{
public:
    DebugInfoBase()          = default;
    virtual ~DebugInfoBase() = default;
};

// Thread local debug information is propagated across the forward
// (including async fork tasks) and backward passes and is supposed
// to be utilized by the user's code to pass extra information from
// the higher layers (e.g. model id) down to the lower levels
// (e.g. to the operator observers used for debugging, logging,
// profiling, etc)
class PROFILER_API thread_local_debug_info
{
public:
    static DebugInfoBase* get(DebugInfoKind kind);

    // Get current thread_local_debug_info
    static std::shared_ptr<thread_local_debug_info> current();

    // Internal, use DebugInfoGuard/ThreadLocalStateGuard
    static void _forceCurrentDebugInfo(std::shared_ptr<thread_local_debug_info> info);

    // Push debug info struct of a given kind
    static void _push(DebugInfoKind kind, std::shared_ptr<DebugInfoBase> info);
    // Pop debug info, throws in case the last pushed
    // debug info is not of a given kind
    static std::shared_ptr<DebugInfoBase> _pop(DebugInfoKind kind);
    // Peek debug info, throws in case the last pushed debug info is not of the
    // given kind
    static std::shared_ptr<DebugInfoBase> _peek(DebugInfoKind kind);

private:
    std::shared_ptr<DebugInfoBase>           info_;
    DebugInfoKind                            kind_;
    std::shared_ptr<thread_local_debug_info> parent_info_;

    friend class DebugInfoGuard;
};

// DebugInfoGuard is used to set debug information,
// thread_local_debug_info is semantically immutable, the values are set
// through the scope-based guard object.
// Nested DebugInfoGuard adds/overrides existing values in the scope,
// restoring the original values after exiting the scope.
// Users can access the values through the thread_local_debug_info::get() call;
class PROFILER_API DebugInfoGuard
{
public:
    DebugInfoGuard(DebugInfoKind kind, std::shared_ptr<DebugInfoBase> info);

    explicit DebugInfoGuard(std::shared_ptr<thread_local_debug_info> info);

    ~DebugInfoGuard();

    DebugInfoGuard(const DebugInfoGuard&)            = delete;
    DebugInfoGuard(DebugInfoGuard&&)                 = delete;
    DebugInfoGuard& operator=(const DebugInfoGuard&) = delete;
    DebugInfoGuard& operator=(DebugInfoGuard&&)      = delete;

private:
    bool                                     active_    = false;
    std::shared_ptr<thread_local_debug_info> prev_info_ = nullptr;
};

}  // namespace profiler
