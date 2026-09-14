#pragma once

#include <cstddef>
#include <cstdint>
#include <list>
#include <optional>
#include <string>
#include <unordered_map>
#include <variant>
#include <vector>

#include "bespoke/common/record_function.h"
#include "common/profiler_export.h"
#include "common/profiler_macros.h"
//#include "util/hash.h"

// #include <profiler/csrc/jit/frontend/source_range.h>
// These are Profiler-specific headers not available in Profiler

// TODO: replace with pytorch/rfcs#43 when it is ready.
#define SOFT_ASSERT(cond, ...)                                                   \
    [&]() -> bool                                                                \
    {                                                                            \
        if PROFILER_UNLIKELY (!(cond))                                           \
        {                                                                        \
            profiler::profiler_impl::impl::logSoftAssert(                        \
                __func__, __FILE__, static_cast<uint32_t>(__LINE__), #cond, ""); \
            return false;                                                        \
        }                                                                        \
        return true;                                                             \
    }()

namespace profiler::detail
{
struct CompileTimeEmptyString
{
    operator const std::string&() const
    {
        static const std::string empty_string_literal;
        return empty_string_literal;
    }
    operator const char*() const { return ""; }
};
}  // namespace profiler::detail

namespace profiler::profiler_impl::impl
{
PROFILER_API bool softAssertRaises();
PROFILER_API void setSoftAssertRaises(std::optional<bool> value);
PROFILER_API void logSoftAssert(
    const char* func, const char* file, uint32_t line, const char* cond, const char* args);
//TODO: Profiler-specific functions commented out
inline void logSoftAssert(
    const char*                                func,
    const char*                                file,
    uint32_t                                   line,
    const char*                                cond,
    ::profiler::detail::CompileTimeEmptyString args)
{
    logSoftAssert(func, file, line, cond, (const char*)args);
}
PROFILER_API void logSoftAssert(
    const char* func, const char* file, uint32_t line, const char* cond, const std::string& args);

using shape = std::variant<std::vector<int64_t>, std::vector<std::vector<int64_t>>>;
constexpr int TENSOR_LIST_DISPLAY_LENGTH_LIMIT = 30;

std::string getNvtxStr(
    const char*                                                      name,
    int64_t                                                          sequence_nr,
    const std::vector<std::vector<int64_t>>&                         shapes,
    profiler::RecordFunctionHandle                                   op_id        = 0,
    const std::list<std::pair<profiler::RecordFunctionHandle, int>>& input_op_ids = {});

PROFILER_API std::string stacksToStr(const std::vector<std::string>& stacks, const char* delim);
PROFILER_API std::vector<std::vector<int64_t>> inputSizes(
    const profiler::RecordFunction& fn, const bool flatten_list_enabled = false);
PROFILER_API std::string variantShapesToStr(const std::vector<shape>& shapes);
PROFILER_API std::string shapesToStr(const std::vector<std::vector<int64_t>>& shapes);
PROFILER_API std::string strListToStr(const std::vector<std::string>& types);
PROFILER_API std::string inputOpIdsToStr(
    const std::list<std::pair<profiler::RecordFunctionHandle, int>>& input_op_ids);
PROFILER_API std::vector<std::string> inputTypes(const profiler::RecordFunction& fn);

std::string shapeToStr(const std::vector<int64_t>& shape);

template <typename T>
class PROFILER_VISIBILITY GlobalStateManager
{
public:
    static GlobalStateManager& singleton()
    {
        /* library-local */ static GlobalStateManager singleton_;
        return singleton_;
    }

    static void push(std::shared_ptr<T>&& state)
    {
        if (singleton().state_)
        {
            //LOG(WARNING) << "GlobalStatePtr already exists!";
        }
        else
        {
            singleton().state_ = std::move(state);
        }
    }

    static auto* get() { return singleton().state_.get(); }

    static std::shared_ptr<T> pop()
    {
        auto out = singleton().state_;
        singleton().state_.reset();
        return out;
    }

private:
    GlobalStateManager() = default;

    std::shared_ptr<T> state_;
};

// struct HashCombine
// {
//     template <typename T0, typename T1>
//     size_t operator()(const std::pair<T0, T1>& i)
//     {
//         return profiler::get_hash((*this)(i.first), (*this)(i.second));
//     }

//     template <typename... Args>
//     size_t operator()(const std::tuple<Args...>& i)
//     {
//         return profiler::get_hash(i);
//     }

//     template <typename T>
//     size_t operator()(const T& i)
//     {
//         return profiler::get_hash(i);
//     }
// };

}  // namespace profiler::profiler_impl::impl
