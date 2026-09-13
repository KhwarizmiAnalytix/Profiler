//----------------------------------------------------------------------------
#if __cplusplus >= 201703L
#define PROFILER_UNUSED [[maybe_unused]]
#elif defined(__GNUC__) || defined(__clang__)
// For GCC or Clang: use __attribute__
#define PROFILER_UNUSED __attribute__((unused))
#elif defined(_MSC_VER)
// For MSVC
#define PROFILER_UNUSED __pragma(warning(suppress : 4100))
#else
// Fallback for other compilers
#define PROFILER_UNUSED
#endif

//----------------------------------------------------------------------------
#if defined(__cplusplus) && defined(__has_cpp_attribute)
#define PROFILER_HAVE_CPP_ATTRIBUTE(x) __has_cpp_attribute(x)
#else
#define PROFILER_HAVE_CPP_ATTRIBUTE(x) 0
#endif

//------------------------------------------------------------------------
// C++20 [[likely]] and [[unlikely]] attributes are only available in C++20 and later
#if __cplusplus >= 202002L && PROFILER_HAVE_CPP_ATTRIBUTE(likely) && \
    PROFILER_HAVE_CPP_ATTRIBUTE(unlikely)
#define PROFILER_LIKELY(expr) (expr) [[likely]]
#define PROFILER_UNLIKELY(expr) (expr) [[unlikely]]
#elif defined(__GNUC__) || defined(__ICL) || defined(__clang__)
#define PROFILER_LIKELY(expr) (__builtin_expect(static_cast<bool>(expr), 1))
#define PROFILER_UNLIKELY(expr) (__builtin_expect(static_cast<bool>(expr), 0))
#else
#define PROFILER_LIKELY(expr) (expr)
#define PROFILER_UNLIKELY(expr) (expr)
#endif

//----------------------------------------------------------------------------
#ifdef __has_attribute
#define PROFILER_HAVE_ATTRIBUTE(x) __has_attribute(x)
#else
#define PROFILER_HAVE_ATTRIBUTE(x) 0
#endif

//----------------------------------------------------------------------------
#if PROFILER_HAVE_CPP_ATTRIBUTE(clang::lifetimebound)
#define PROFILER_LIFETIME_BOUND [[clang::lifetimebound]]
#elif PROFILER_HAVE_CPP_ATTRIBUTE(msvc::lifetimebound)
#define PROFILER_LIFETIME_BOUND [[msvc::lifetimebound]]
#elif PROFILER_HAVE_ATTRIBUTE(lifetimebound)
#define PROFILER_LIFETIME_BOUND __attribute__((lifetimebound))
#else
#define PROFILER_LIFETIME_BOUND
#endif

//----------------------------------------------------------------------------
// Thread safety - guarded by mutex
#if PROFILER_HAVE_ATTRIBUTE(guarded_by)
#define PROFILER_GUARDED_BY(x) __attribute__((guarded_by(x)))
#else
#define PROFILER_GUARDED_BY(x)
#endif

//----------------------------------------------------------------------------
#if __cplusplus >= 201703L
#define PROFILER_NODISCARD [[nodiscard]]
#else
#define PROFILER_NODISCARD
#endif

//----------------------------------------------------------------------------
#if PROFILER_HAVE_CPP_ATTRIBUTE(clang::require_constant_initialization)
#define PROFILER_CONST_INIT [[clang::require_constant_initialization]]
#else
#define PROFILER_CONST_INIT
#endif

//----------------------------------------------------------------------------
#define PROFILER_CONCATENATE(s1, s2) s1##s2
#ifdef __COUNTER__
#define PROFILER_UID __COUNTER__
#define PROFILER_ANONYMOUS_VARIABLE(str) PROFILER_CONCATENATE(str, __COUNTER__)
#else
#define PROFILER_UID __LINE__
#define PROFILER_ANONYMOUS_VARIABLE(str) PROFILER_CONCATENATE(str, __LINE__)
#endif

//----------------------------------------------------------------------------
#include <stdexcept>
#define PROFILER_THROW(msg) throw std::runtime_error(msg)

//----------------------------------------------------------------------------
#define PROFILER_LOG_ERROR(...)

//----------------------------------------------------------------------------
#if defined(__GNUC__) || defined(__clang__)
#define PROFILER_USED __attribute__((used))
#else
#define PROFILER_USED
#endif