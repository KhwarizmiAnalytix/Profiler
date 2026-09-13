#pragma once

#include <array>
#include <cstring>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "common/profiler_export.h"
#include "common/small_vector.h"

namespace profiler
{

// Kind of record function scope.
enum class RecordScope : uint8_t
{
    FUNCTION = 0,
    BACKWARD_FUNCTION,
    USER_SCOPE,
    NUM_SCOPES,  // must be the last in the list
};

}  // namespace profiler

namespace std
{
template <>
struct hash<profiler::RecordScope>
{
    size_t operator()(const profiler::RecordScope& sc) const
    {
        return static_cast<std::size_t>(sc);
    }
};
}  // namespace std

namespace profiler
{

struct PROFILER_VISIBILITY StringView
{
    StringView() : StringView(nullptr) {}
    explicit StringView(const char* str_ptr) : owned_str_ptr_(nullptr), str_ptr_(str_ptr) {}
    explicit StringView(std::string str)
        : owned_str_ptr_(std::make_shared<std::string>(std::move(str))),
          str_ptr_(owned_str_ptr_->c_str())
    {
    }

    const char* str() const { return str_ptr_; }

    friend std::ostream& operator<<(std::ostream& os, const StringView& dt)
    {
        os << dt.str();
        return os;
    }

    friend bool operator==(const StringView& lhs, const StringView& rhs)
    {
        return strcmp(lhs.str(), rhs.str()) == 0;
    }

    friend bool operator!=(const StringView& lhs, const StringView& rhs) { return !(lhs == rhs); }

private:
    std::shared_ptr<std::string> owned_str_ptr_;
    const char*                  str_ptr_;
};

// Soft limit on the number of callbacks to use;
constexpr std::size_t kSoftLimitCallbacks = 4;

// An abstract base class for various observer contexts that can be attached to
// the RecordFunction.
struct ObserverContext
{
    virtual ~ObserverContext() = default;

protected:
    ObserverContext() = default;
};

typedef profiler::small_vector<uint64_t, kSoftLimitCallbacks> CallbackHandles;
typedef profiler::small_vector<std::unique_ptr<ObserverContext>, kSoftLimitCallbacks>
                 ObserverContextList;
typedef uint64_t RecordFunctionHandle;
struct RecordFunction;

//
// Profiler callbacks/observers API:
//

/**
 * RecordFunctionCallback represents a pair of callbacks to be used with
 * RecordFunction, members:
 *   start, end - the callbacks to run when entering and exiting the scope;
 *     optionally, the start callback may return an ObserverContext which will
 *     be passed to the end callback, use appropriate constructor accordingly.
 *   needs_inputs - whether the callbacks need the inputs passed from the
 * observed function/range; NOTE: passing the inputs incurs an additional
 * overhead; sampling_probability - if not 1.0, then the callback is
 * probabilistically sampled to run; NOTE: start and end callbacks always run as
 * a pair and are sampled together; scopes - types of scopes to execute the
 * callbacks on (see RecordScope); passing empty set means the callbacks will be
 * executed for all possible scope types should_run - optional function that
 * returns whether this callback should run; overwrites the effect of setting
 * sampling_probability
 */
class PROFILER_VISIBILITY RecordFunctionCallback
{
public:
    using StartCallback = std::unique_ptr<ObserverContext> (*)(const RecordFunction&);
    using EndCallback   = void (*)(const RecordFunction&, ObserverContext*);

    // This interface supports observers that require passing an ObserverContext
    // between start and end callbacks.
    explicit RecordFunctionCallback(StartCallback start, EndCallback end = nullptr)
        : start_(start), end_(end)
    {
        scopes_.fill(true);
    }

    RecordFunctionCallback& needsInputs(bool needs_inputs)
    {
        needs_inputs_ = needs_inputs;
        return *this;
    }

    RecordFunctionCallback& needsOutputs(bool needs_outputs)
    {
        needs_outputs_ = needs_outputs;
        return *this;
    }

    RecordFunctionCallback& needsIds(bool needs_ids)
    {
        needs_ids_ = needs_ids;
        return *this;
    }

    RecordFunctionCallback& samplingProb(double sampling_prob)
    {
        // PROFILER_CHECK(
        // sampling_prob >= 0.0 && sampling_prob <= 1.0, "Invalid sampling probability");
        sampling_prob_ = sampling_prob;
        return *this;
    }

    RecordFunctionCallback& scopes(
        const std::unordered_set<RecordScope, std::hash<RecordScope>>& scopes)
    {
        if (!scopes.empty())
        {
            scopes_.fill(false);
            for (auto sc : scopes)
            {
                scopes_[static_cast<size_t>(sc)] = true;
            }
        }
        else
        {
            scopes_.fill(true);
        }
        return *this;
    }

    bool needsInputs() const { return needs_inputs_; }

    bool needsOutputs() const { return needs_outputs_; }

    bool needsIds() const { return needs_ids_; }

    double samplingProb() const { return sampling_prob_; }

    bool checkScope(RecordScope sc) const { return scopes_[(size_t)sc]; }

    StartCallback start() const { return start_; }

    EndCallback end() const { return end_; }

private:
    StartCallback                                                  start_;
    EndCallback                                                    end_;
    double                                                         sampling_prob_ = 1.0;
    std::array<bool, static_cast<size_t>(RecordScope::NUM_SCOPES)> scopes_        = {};
    bool                                                           needs_inputs_  = false;
    bool                                                           needs_outputs_ = false;
    bool                                                           needs_ids_     = false;
};

// Notes:
//  - two types of callbacks are provided: thread local and global
//     - thread local callbacks are added/removed only for the given thread
//       and are stored locally for each thread and separately from the list
//       of the global callbacks
//     - global callbacks are stored in a single per process list and are
//       invoked by every RecordFunction, in addition to the thread local
//       callbacks specific to the given thread
//  - we allow the added callbacks to be sampled, by specifying a sampling
//    probability for each callback pair, if the start callback is
//    not picked to run, the corresponding end callback won't be called
//  - a typical use case for the global callbacks is passive monitoring
//    in the background (e.g. fleet-wide monitoring), without focusing on
//    the specific piece of code
//  - in contrast, thread local callbacks are enabled locally, on demand,
//    for the specific piece of code (range) and are not sampled
//  - a typical use case for thread local callbacks is profiler and code
//    execution tracer
//  - note, thread local callbacks are automatically propagated with
//    ThreadLocalState across JIT continuations and async tasks (profiler::launch)

typedef uint64_t CallbackHandle;

constexpr CallbackHandle INVALID_CALLBACK_HANDLE{0};

// It is unnecessary to use atomic operations for enabling
// thread-local function callbacks. Moreover, it prevents saving to
// ThreadLocalState because std::atomic is non-copyable.
struct RecordFunctionCallbacksEntry
{
    RecordFunctionCallbacksEntry(RecordFunctionCallback cb, CallbackHandle h)
        : callback_(cb), handle_(h)
    {
    }

    RecordFunctionCallback callback_;
    bool                   enabled_{true};
    CallbackHandle         handle_;
};

// Holds pairs (callbacks, unique_id)
using RecordFunctionCallbacks = std::vector<RecordFunctionCallbacksEntry>;

// Generated by the callback managers to determine which functions to run.
struct StepCallbacks
{
    StepCallbacks() = default;
    StepCallbacks(uint64_t thread_id, RecordScope scope) : thread_id_{thread_id}, scope_{scope} {}

    bool empty() const { return callbacks_.empty(); }

    struct StartEndPair
    {
        RecordFunctionCallback::StartCallback start_;
        RecordFunctionCallback::EndCallback   end_;
    };

    using StartEndPairs = profiler::small_vector<StartEndPair, kSoftLimitCallbacks>;

    StartEndPairs callbacks_;
    uint64_t      thread_id_{0};
    RecordScope   scope_{RecordScope::FUNCTION};
    bool          needs_inputs_{false};
    bool          needs_outputs_{false};
    bool          needs_ids_{false};
};

struct PROFILER_VISIBILITY RecordFunction
{
    PROFILER_API explicit RecordFunction(RecordScope scope = RecordScope::FUNCTION);
    PROFILER_API explicit RecordFunction(StepCallbacks&& step_callbacks);

    PROFILER_API virtual ~RecordFunction();

    RecordFunction(const RecordFunction&)            = delete;
    RecordFunction& operator=(const RecordFunction&) = delete;
    RecordFunction(RecordFunction&&)                 = delete;
    RecordFunction& operator=(RecordFunction&&)      = delete;

    PROFILER_API const char*        name() const;
    static PROFILER_API const char* overload_name();

    int64_t seqNr() const { return sequence_nr_; }

    // Thread that ran start callbacks. End callbacks may run on a different
    // thread for async ops.
    uint64_t threadId() const { return step_callbacks_.thread_id_; }

    uint64_t forwardThreadId() const { return fwd_thread_id_; }

    void setForwardThreadId(uint64_t thread_id) { fwd_thread_id_ = thread_id; }

    RecordScope scope() const { return step_callbacks_.scope_; }

    PROFILER_API static uint64_t currentThreadId();

    PROFILER_API void before(std::string_view name, int64_t sequence_nr = -1);

    PROFILER_API static void    setDefaultNodeId(int64_t defaultNodeId);
    PROFILER_API static int64_t getDefaultNodeId();

    PROFILER_API void end();

    void _setAsync();
    bool isAsync() const;

    RecordFunctionHandle handle() const { return handle_; }

    void setHandle(RecordFunctionHandle handle) { handle_ = handle; }

    bool isActive() const { return !step_callbacks_.empty(); }

    bool needsInputs() const { return step_callbacks_.needs_inputs_; }

    bool needsOutputs() const { return step_callbacks_.needs_outputs_; }

    int64_t debugHandle() const { return debug_handle_; }

    void setDebugHandle(int64_t debug_handle) { debug_handle_ = debug_handle; }

    void setSourceLocation(const char* file, uint32_t line)
    {
        source_file_ = file;
        source_line_ = line;
    }

    const char* sourceFile() const { return source_file_; }

    uint32_t sourceLine() const { return source_line_; }

    // Structured per-call metadata: arbitrary caller-supplied key/value pairs,
    // surfaced downstream via KinetoEvent::extraMeta(). This is the generic
    // replacement for op-specific (tensor/IValue-shaped) argument recording --
    // see record_function_metadata_builder for the ergonomic call-site API.
    void addMetadata(std::string key, std::string value)
    {
        metadata_[std::move(key)] = std::move(value);
    }

    const std::unordered_map<std::string, std::string>& metadata() const { return metadata_; }

private:
    void runStartCallbacks();

    StepCallbacks                                step_callbacks_;
    bool                                         called_start_callbacks_ = false;
    ObserverContextList                          ctx_;
    std::string                                  fn_;
    int64_t                                      sequence_nr_   = -1;
    uint64_t                                     fwd_thread_id_ = 0;
    RecordFunctionHandle                         handle_{0};
    bool                                         is_async_{false};
    int64_t                                      debug_handle_{-1};
    const char*                                  source_file_{nullptr};
    uint32_t                                     source_line_{0};
    std::unordered_map<std::string, std::string> metadata_;
};

PROFILER_API StepCallbacks getStepCallbacks(RecordScope scope);

PROFILER_API std::optional<StepCallbacks> getStepCallbacksUnlessEmpty(RecordScope scope);

// PROFILER_RECORD_* (not RECORD_*) so these do not collide with LibTorch's
// ATen/record_function.h macros of the same unprefixed names.
#define PROFILER_RECORD_FUNCTION_WITH_SCOPE(scope, fn) \
    profiler::RecordFunction guard(scope);             \
    guard.setSourceLocation(__FILE__, __LINE__);       \
    if (guard.isActive())                              \
    {                                                  \
        guard.before(fn);                              \
    }

#define PROFILER_RECORD_FUNCTION(fn) \
    PROFILER_RECORD_FUNCTION_WITH_SCOPE(profiler::RecordScope::FUNCTION, fn)

#define PROFILER_RECORD_USER_SCOPE(fn) \
    PROFILER_RECORD_FUNCTION_WITH_SCOPE(profiler::RecordScope::USER_SCOPE, fn)

/**
 * @brief Fluent builder for attaching structured metadata to a RecordFunction
 * guard, e.g.:
 *
 *   PROFILER_RECORD_FUNCTION_WITH_METADATA(guard, "gemm")
 *       .with_metadata("m", m).with_metadata("n", n).with_metadata("k", k);
 *
 * Values are stringified into the same key/value map surfaced via
 * KinetoEvent::extraMeta() -- this is a generic mechanism for profiling any
 * function's parameters, not a tensor/IValue-shaped argument list.
 *
 * `guard` is constructed but not yet started (see the macro below); this
 * builder starts it -- calling RecordFunction::before(), which synchronously
 * runs the profiler's start callbacks -- only once its own lifetime ends
 * (its destructor fires after every chained with_metadata() call in the
 * same full expression), so metadata set here is visible to those
 * callbacks. Starting `guard` itself, rather than in the macro, would run
 * the callbacks before any metadata had been attached.
 */
class PROFILER_VISIBILITY record_function_metadata_builder
{
public:
    record_function_metadata_builder(RecordFunction& fn, std::string_view name)
        : fn_(fn), name_(name)
    {
    }

    record_function_metadata_builder(const record_function_metadata_builder&)            = delete;
    record_function_metadata_builder& operator=(const record_function_metadata_builder&) = delete;
    record_function_metadata_builder(record_function_metadata_builder&&)                 = delete;
    record_function_metadata_builder& operator=(record_function_metadata_builder&&)      = delete;

    ~record_function_metadata_builder()
    {
        if (fn_.isActive())
        {
            fn_.before(name_);
        }
    }

    record_function_metadata_builder& with_metadata(std::string key, std::string value)
    {
        fn_.addMetadata(std::move(key), std::move(value));
        return *this;
    }

    record_function_metadata_builder& with_metadata(std::string key, int64_t value)
    {
        return with_metadata(std::move(key), std::to_string(value));
    }

    record_function_metadata_builder& with_metadata(std::string key, double value)
    {
        return with_metadata(std::move(key), std::to_string(value));
    }

private:
    RecordFunction&  fn_;
    std::string_view name_;
};

// Declares `guard_name` (scope: the enclosing block, like PROFILER_RECORD_FUNCTION)
// without starting it. Chain record_function_metadata_builder(guard_name, fn)
// .with_metadata(...) immediately after to attach metadata and start it --
// see the class comment above for why start is deferred to the builder.
#define PROFILER_RECORD_FUNCTION_WITH_METADATA(guard_name, fn)            \
    profiler::RecordFunction guard_name(profiler::RecordScope::FUNCTION); \
    guard_name.setSourceLocation(__FILE__, __LINE__)

/**
 * addThreadLocalCallback adds a thread local callback to run with
 * RecordFunction, returns handle to use with removeThreadLocalCallback
 */
PROFILER_API CallbackHandle addThreadLocalCallback(RecordFunctionCallback cb);

/**
 * hasThreadLocalCallbacks returns whether there're callbacks registered
 * with addThreadLocalCallback
 */
PROFILER_API bool hasThreadLocalCallbacks();

/**
 * clearThreadLocalCallbacks removes all thread local callbacks
 */
PROFILER_API void clearThreadLocalCallbacks();

/**
 * addGlobalCallback adds a global callback to run with RecordFunction:
 *
 * only during the program initialization
 */
PROFILER_API CallbackHandle addGlobalCallback(RecordFunctionCallback cb);

/**
 * removeCallback removes a callback given the handle returned by
 * addThreadLocalCallback or addGlobalCallback;
 *
 * no other code can run simultaneously
 */
PROFILER_API void removeCallback(CallbackHandle handle);

/**
 * Prevent the given callback from executing. If handle is invalid,
 * does nothing.
 */
PROFILER_API void disableCallback(CallbackHandle handle);

/**
 * Allow the given callback, previously disabled with disableCallback, to
 * execute again. If handle is invalid, does nothing.
 */
PROFILER_API void reenableCallback(CallbackHandle handle);

/**
 * hasGlobalCallbacks returns whether there're global callbacks
 * registered with pushGlobalCallback
 */
PROFILER_API bool hasGlobalCallbacks();

/**
 * clearGlobalCallbacks removes all global callbacks
 */
PROFILER_API void clearGlobalCallbacks();

// for both thread local and global callbacks
PROFILER_API bool hasCallbacks();
PROFILER_API void clearCallbacks();

/**
 * enableRecordFunction enables RecordFunction thread locally
 */
PROFILER_API void enableRecordFunction(bool enable = true);

/**
 * isRecordFunctionEnabled returns whether RecordFunction
 * is enabled thread locally
 */
PROFILER_API bool isRecordFunctionEnabled();

class PROFILER_VISIBILITY RecordFunctionGuard
{
public:
    explicit RecordFunctionGuard(bool is_enabled = true) : prev_value_(isRecordFunctionEnabled())
    {
        enableRecordFunction(is_enabled);
    }

    RecordFunctionGuard(RecordFunctionGuard&& other)           = delete;
    RecordFunctionGuard(const RecordFunctionGuard&)            = delete;
    RecordFunctionGuard& operator=(const RecordFunctionGuard&) = delete;
    RecordFunctionGuard& operator=(RecordFunctionGuard&&)      = delete;
    virtual ~RecordFunctionGuard() { enableRecordFunction(prev_value_); }

private:
    bool prev_value_ = false;
};

class PROFILER_VISIBILITY DisableRecordFunctionGuard : public RecordFunctionGuard
{
public:
    DisableRecordFunctionGuard() : RecordFunctionGuard(false) {}
    ~DisableRecordFunctionGuard() override = default;
};

struct PROFILER_VISIBILITY RecordFunctionTLS
{
    // Thread local vector of callbacks, holds pairs (callbacks, unique_id);
    // must be sorted in increasing handles order
    RecordFunctionCallbacks sorted_tls_callbacks_;

    bool tls_record_function_enabled_ = true;
};

PROFILER_API const RecordFunctionTLS& get_record_function_tls_();

PROFILER_API void set_record_function_tls_(const RecordFunctionTLS& tls);

PROFILER_API void set_record_function_seed_for_testing(uint32_t seed);

}  // namespace profiler
