//#include <profiler/csrc/autograd/function.h>

#include "bespoke/common/util.h"

#include <fmt/format.h>
#include <fmt/ranges.h>

#include <sstream>

#include "bespoke/base/thread_local_debug_info.h"
#include "bespoke/common/collection.h"
#include "common/array_ref.h"
#include "common/irange.h"

#if PROFILER_HAS_KINETO
#include <libkineto.h>
#endif

namespace profiler::profiler_impl::impl
{

namespace
{
std::optional<bool> soft_assert_raises_;
}  // namespace

void setSoftAssertRaises(std::optional<bool> value)
{
    soft_assert_raises_ = value;
}

bool softAssertRaises()
{
    return soft_assert_raises_.value_or(false);
}

void logSoftAssert(
    // @lint-ignore CLANGTIDY
    const char* func,
    // @lint-ignore CLANGTIDY
    const char* file,
    // @lint-ignore CLANGTIDY
    uint32_t line,
    // @lint-ignore CLANGTIDY
    const char* cond,
    // @lint-ignore CLANGTIDY
    const char* args)
{
#if PROFILER_HAS_KINETO
    std::string error;
    error = fmt::format(
        "{} SOFT ASSERT FAILED at {}:{}, func: {}, args: {}", cond, file, line, func, args);
    // TODO: Implement profile_id and group_profile_id as 3rd/4th arguments.
    kineto::logInvariantViolation(cond, error, "", "");
#endif
}

void logSoftAssert(
    // @lint-ignore CLANGTIDY
    const char* func,
    // @lint-ignore CLANGTIDY
    const char* file,
    // @lint-ignore CLANGTIDY
    uint32_t line,
    // @lint-ignore CLANGTIDY
    const char* cond,
    // @lint-ignore CLANGTIDY
    const std::string& args)
{
#if PROFILER_HAS_KINETO
    std::string error;
    error = fmt::format(
        "{} SOFT ASSERT FAILED at {}:{}, func: {}, args: {}", cond, file, line, func, args);
    // TODO: Implement profile_id and group_profile_id as 3rd/4th arguments.
    kineto::logInvariantViolation(cond, error, "", "");
#endif
}

// ----------------------------------------------------------------------------
// -- NVTX --------------------------------------------------------------------
// ----------------------------------------------------------------------------
std::string getNvtxStr(
    const char*                                                      name,
    int64_t                                                          sequence_nr,
    const std::vector<std::vector<int64_t>>&                         shapes,
    profiler::RecordFunctionHandle                                   op_id,
    const std::list<std::pair<profiler::RecordFunctionHandle, int>>& input_op_ids)
{
    if (sequence_nr >= -1 || !shapes.empty())
    {
        std::string str;
        if (sequence_nr >= 0)
        {
            str = fmt::format("{}, seq = {}", name, sequence_nr);
        }
        else if (sequence_nr == -1)
        {
            str = name;
        }
        else
        {
#ifdef PROFILER_USE_ROCM
            // Only ROCM supports < -1 sequence_nr
            str = name;
#endif
        }
        if (op_id > 0)
        {
            str = fmt::format("{}, op_id = {}", str, op_id);
        }
        if (!shapes.empty())
        {
            str = fmt::format("{}, sizes = {}", str, shapesToStr(shapes));
        }
        // Include the op ids of the input edges so
        // you can build the network graph
        if (!input_op_ids.empty())
        {
            str = fmt::format("{}, input_op_ids = {}", str, inputOpIdsToStr(input_op_ids));
        }
        return str;
    }

    return name;
}

std::string stacksToStr(const std::vector<std::string>& stacks, const char* delim)
{
    std::ostringstream oss;
    std::transform(
        stacks.begin(),
        stacks.end(),
        std::ostream_iterator<std::string>(oss, delim),
        [](const std::string& s) -> std::string
        {
#ifdef _WIN32
            // replace the windows backslash with forward slash
            std::string result = s;
            std::replace(result.begin(), result.end(), '\\', '/');
            return result;
#else
            return s;
#endif
        });
    auto rc = oss.str();
    return "\"" + rc + "\"";
}

// XSigma has no tensor type; this always returns empty.
std::vector<std::vector<int64_t>> inputSizes(
    const profiler::RecordFunction& /*fn*/, bool /*flatten_list_enabled*/)
{
    return {};
}

std::string shapesToStr(const std::vector<std::vector<int64_t>>& shapes)
{
    std::string str("[");
    for (const auto t_idx : profiler::irange(shapes.size()))
    {
        if (t_idx > 0)
        {
            str = fmt::format("{}, ", str);
        }
        str = fmt::format("{}{}", str, shapeToStr(shapes[t_idx]));
    }
    str = fmt::format("{}]", str);
    return str;
}

std::string variantShapesToStr(const std::vector<shape>& shapes)
{
    std::string str("[");
    for (const auto t_idx : profiler::irange(shapes.size()))
    {
        if (t_idx > 0)
        {
            str = fmt::format("{}, ", str);
        }
        if (std::holds_alternative<std::vector<int64_t>>(shapes[t_idx]))
        {
            const auto& shape = std::get<std::vector<int64_t>>(shapes[t_idx]);
            str               = fmt::format("{}{}", str, shapeToStr(shape));
        }
        else if (std::holds_alternative<std::vector<std::vector<int64_t>>>(shapes[t_idx]))
        {
            const auto& tensor_shape = std::get<std::vector<std::vector<int64_t>>>(shapes[t_idx]);
            if (tensor_shape.size() > TENSOR_LIST_DISPLAY_LENGTH_LIMIT)
            {
                // skip if the tensor list is too long
                str = fmt::format("{}[]", str);
                continue;
            }
            str = fmt::format("{}[", str);
            for (const auto s_idx : profiler::irange(tensor_shape.size()))
            {
                if (s_idx > 0)
                {
                    str = fmt::format("{}, ", str);
                }
                str = fmt::format("{}{}", str, shapeToStr(tensor_shape[s_idx]));
            }
            str = fmt::format("{}]", str);
        }
    }
    str = fmt::format("{}]", str);
    return str;
}

std::string shapeToStr(const std::vector<int64_t>& shape)
{
    std::string str("[");
    for (const auto s_idx : profiler::irange(shape.size()))
    {
        if (s_idx > 0)
        {
            str = fmt::format("{}, ", str);
        }
        str = fmt::format("{}{}", str, shape[s_idx]);
    }
    str = fmt::format("{}]", str);
    return str;
}

std::string inputOpIdsToStr(
    const std::list<std::pair<profiler::RecordFunctionHandle, int>>& input_op_ids)
{
    std::string str("[");
    int         idx = 0;

    for (const auto& op_id_info_pair : input_op_ids)
    {
        if (idx++ > 0)
        {
            str = fmt::format("{}, ", str);
        }
        // (OpId,OutputNr)
        str = fmt::format("{}({},{})", str, op_id_info_pair.first, op_id_info_pair.second);
    }
    str = fmt::format("{}]", str);
    return str;
}

std::string strListToStr(const std::vector<std::string>& types)
{
    if (types.empty())
    {
        return "[]";
    }

    std::ostringstream oss;
    std::transform(
        types.begin(),
        types.end(),
        std::ostream_iterator<std::string>(oss, ", "),
        [](const std::string& s) -> std::string { return "\"" + s + "\""; });
    auto rc = oss.str();
    rc.erase(rc.length() - 2);  // remove last ", "
    return "[" + rc + "]";
}

std::vector<std::string> inputTypes(const profiler::RecordFunction& /*fn*/)
{
    return {};
}

}  // namespace profiler::profiler_impl::impl
