/*
 * Profiler
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     https://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

// Unit tests for the low-level native/utils and native/tracing helpers:
// annotation parsing, unit conversions, formatting, trace-viewer constants,
// and TraceMe metadata encoding. Otherwise only exercised indirectly.

#include <cstdint>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>

#if defined(_WIN32)
#include <direct.h>
#else
#include <sys/stat.h>
#endif

#include "ProfilerTest.h"
#include "native/tracing/traceme_encode.h"
#include "native/utils/checked_file_write.h"
#include "native/utils/format_utils.h"
#include "native/utils/math_utils.h"
#include "native/utils/parse_annotation.h"
#include "native/utils/time_utils.h"
#include "native/utils/trace_utils.h"

// ---------------------------------------------------------------------------
// parse_annotation
// ---------------------------------------------------------------------------

PROFILERTEST(ParseAnnotation, has_metadata_detects_trailing_marker)
{
    using profiler::profiler_impl::has_metadata;
    EXPECT_TRUE(has_metadata("name#key=value#"));
    EXPECT_FALSE(has_metadata("name"));
    EXPECT_FALSE(has_metadata(""));
}

PROFILERTEST(ParseAnnotation, parses_name_and_metadata_pairs)
{
    auto result = profiler::profiler_impl::parse_annotation("matrix_multiply#rows=100,cols=200#");
    EXPECT_EQ(result.name, "matrix_multiply");
    ASSERT_EQ(result.metadata.size(), 2u);
    EXPECT_EQ(result.metadata[0].key, "rows");
    EXPECT_EQ(result.metadata[0].value, "100");
    EXPECT_EQ(result.metadata[1].key, "cols");
    EXPECT_EQ(result.metadata[1].value, "200");
}

PROFILERTEST(ParseAnnotation, name_without_metadata_marker_has_empty_metadata)
{
    auto result = profiler::profiler_impl::parse_annotation("simple_name");
    EXPECT_EQ(result.name, "simple_name");
    EXPECT_TRUE(result.metadata.empty());
}

PROFILERTEST(ParseAnnotation, strips_surrounding_whitespace_from_name_and_pairs)
{
    auto result = profiler::profiler_impl::parse_annotation("  spaced_name  # key = value #");
    EXPECT_EQ(result.name, "spaced_name");
    ASSERT_EQ(result.metadata.size(), 1u);
    EXPECT_EQ(result.metadata[0].key, "key");
    EXPECT_EQ(result.metadata[0].value, "value");
}

PROFILERTEST(ParseAnnotation, stack_splits_on_double_colon_delimiter)
{
    auto stack =
        profiler::profiler_impl::parse_annotation_stack("outer_func#level=1#::inner_func#level=2#");
    ASSERT_EQ(stack.size(), 2u);
    EXPECT_EQ(stack[0].name, "outer_func");
    ASSERT_EQ(stack[0].metadata.size(), 1u);
    EXPECT_EQ(stack[0].metadata[0].value, "1");
    EXPECT_EQ(stack[1].name, "inner_func");
    ASSERT_EQ(stack[1].metadata.size(), 1u);
    EXPECT_EQ(stack[1].metadata[0].value, "2");
}

PROFILERTEST(ParseAnnotation, stack_skips_empty_segments)
{
    auto stack = profiler::profiler_impl::parse_annotation_stack("a#x=1#::::b#y=2#");
    ASSERT_EQ(stack.size(), 2u);
    EXPECT_EQ(stack[0].name, "a");
    EXPECT_EQ(stack[1].name, "b");
}

// ---------------------------------------------------------------------------
// math_utils
// ---------------------------------------------------------------------------

PROFILERTEST(MathUtils, time_unit_conversions_round_trip)
{
    using namespace profiler::profiler_impl;
    EXPECT_NEAR(pico_to_nano(5000), 5.0, 1e-9);
    EXPECT_EQ(nano_to_pico(5), 5000u);
    EXPECT_NEAR(micro_to_nano(2.0), 2000.0, 1e-9);
    EXPECT_EQ(milli_to_nano(3.0), 3000000u);
    EXPECT_EQ(uni_to_nano(1.0), 1000000000u);
    EXPECT_NEAR(nano_to_micro(2000), 2.0, 1e-9);
}

PROFILERTEST(MathUtils, safe_divide_avoids_division_by_zero)
{
    using profiler::profiler_impl::safe_divide;
    EXPECT_NEAR(safe_divide(10.0, 2.0), 5.0, 1e-9);
    EXPECT_EQ(safe_divide(10.0, 0.0), 0.0);
    EXPECT_EQ(safe_divide(10.0, 1e-12), 0.0);
}

PROFILERTEST(MathUtils, binary_decimal_byte_conversions_round_trip)
{
    using namespace profiler::profiler_impl;
    const double gibi = 4.0;
    const double giga = gibi_to_giga(gibi);
    EXPECT_NEAR(giga_to_gibi(giga), gibi, 1e-9);
}

PROFILERTEST(MathUtils, gibibytes_per_second_matches_manual_computation)
{
    using profiler::profiler_impl::gibibytes_per_second;
    // 1 GiB transferred in 1e9 ns (1 second) is ~1 GiB/s.
    const double bytes = static_cast<double>(1ull << 30);
    EXPECT_NEAR(gibibytes_per_second(bytes, 1e9), 1.0, 1e-6);
}

// ---------------------------------------------------------------------------
// format_utils
// ---------------------------------------------------------------------------

PROFILERTEST(FormatUtils, fixed_precision_formatting)
{
    using namespace profiler::profiler_impl;
    EXPECT_EQ(one_digit(3.14159), "3.1");
    EXPECT_EQ(two_digits(3.14159), "3.14");
    EXPECT_EQ(three_digits(3.14159), "3.142");
}

PROFILERTEST(FormatUtils, max_precision_round_trips_through_stod)
{
    const double      original = 1.0 / 3.0;
    const std::string text     = profiler::profiler_impl::max_precision(original);
    EXPECT_EQ(std::stod(text), original);
}

// ---------------------------------------------------------------------------
// trace_utils
// ---------------------------------------------------------------------------

PROFILERTEST(TraceUtils, derived_thread_id_range_boundaries)
{
    using namespace profiler::profiler_impl;
    EXPECT_TRUE(is_derived_thread_id(kThreadIdDerivedMin));
    EXPECT_TRUE(is_derived_thread_id(kThreadIdDerivedMax));
    EXPECT_TRUE(is_derived_thread_id(kThreadIdStepInfo));
    EXPECT_FALSE(is_derived_thread_id(kThreadIdDerivedMin - 1));
    EXPECT_FALSE(is_derived_thread_id(kThreadIdDerivedMax + 1));
}

PROFILERTEST(TraceUtils, parse_device_ordinal_handles_common_formats)
{
    using profiler::profiler_impl::parse_device_ordinal;
    EXPECT_EQ(parse_device_ordinal("localhost /device:GPU:0"), 0u);
    EXPECT_EQ(parse_device_ordinal("worker1 /device:CPU:2"), 2u);
    EXPECT_EQ(parse_device_ordinal("/device:GPU:3"), 3u);
    EXPECT_EQ(parse_device_ordinal("GPU:1"), 1u);
    EXPECT_FALSE(parse_device_ordinal("no_colon_here").has_value());
    EXPECT_FALSE(parse_device_ordinal("/device:GPU:not_a_number").has_value());
}

// ---------------------------------------------------------------------------
// traceme_encode / TraceMeArg
// ---------------------------------------------------------------------------

PROFILERTEST(TraceMeEncode, encodes_mixed_argument_types)
{
    std::string encoded =
        profiler::traceme_encode("matrix_op", {{"rows", 100}, {"label", "abc"}, {"ok", true}});
    EXPECT_EQ(encoded, "matrix_op#rows=100,label=abc,ok=true#");
}

PROFILERTEST(TraceMeEncode, empty_args_leaves_name_unchanged)
{
    EXPECT_EQ(profiler::traceme_encode(std::string("plain_name"), {}), "plain_name");
}

PROFILERTEST(TraceMeEncode, null_c_string_value_becomes_empty)
{
    const char* null_value = nullptr;
    std::string encoded    = profiler::traceme_encode("op", {{"key", null_value}});
    EXPECT_EQ(encoded, "op#key=#");
}

PROFILERTEST(TraceMeEncode, append_metadata_merges_into_existing_metadata_section)
{
    std::string name = "op#a=1#";
    profiler::traceme_internal::append_metadata(&name, profiler::traceme_encode({{"b", 2}}));
    EXPECT_EQ(name, "op#a=1,b=2#");
}

PROFILERTEST(TraceMeEncode, traceme_op_formats_name_and_type)
{
    EXPECT_EQ(profiler::traceme_op("MatMul", "GPU"), "MatMul:GPU");
    EXPECT_EQ(profiler::traceme_op_override("MatMul", "Forward"), "#tf_op=MatMul:Forward#");
}

// ---------------------------------------------------------------------------
// time_utils
// ---------------------------------------------------------------------------

PROFILERTEST(TimeUtils, current_time_nanos_is_monotonic_nondecreasing)
{
    const int64_t first  = profiler::profiler_impl::get_current_time_nanos();
    const int64_t second = profiler::profiler_impl::get_current_time_nanos();
    EXPECT_GT(first, 0);
    EXPECT_GE(second, first);
}

PROFILERTEST(TimeUtils, zero_duration_sleep_and_spin_return)
{
    // These should return promptly without hanging; no timing assertions to
    // avoid flakiness under load or sanitizer instrumentation.
    profiler::profiler_impl::sleep_for_nanos(0);
    profiler::profiler_impl::spin_for_nanos(0);
    SUCCEED();
}

// ---------------------------------------------------------------------------
// checked_file_write
// ---------------------------------------------------------------------------

namespace
{
std::string read_file(const std::string& path)
{
    std::ifstream     file(path);
    std::ostringstream contents;
    contents << file.rdbuf();
    return contents.str();
}

bool file_exists(const std::string& path)
{
    std::ifstream file(path);
    return file.good();
}
}  // namespace

PROFILERTEST(CheckedFileWrite, publishes_full_content_on_success)
{
    const std::string path = "checked_file_write_success.txt";
    std::remove(path.c_str());

    ASSERT_TRUE(profiler::profiler_impl::write_file_checked(path, "hello checked write"));
    EXPECT_EQ(read_file(path), "hello checked write");
    // No stray temp file left behind.
    EXPECT_FALSE(file_exists(path + ".tmp"));

    std::remove(path.c_str());
}

PROFILERTEST(CheckedFileWrite, failure_leaves_existing_destination_untouched)
{
    // A directory can't be opened for writing via ofstream -- use that to force
    // the "publish" step (the rename) to fail without needing filesystem
    // permission tricks that would vary across CI platforms.
    const std::string dir_path = "checked_file_write_failure_dir";
    std::remove(dir_path.c_str());
#if defined(_WIN32)
    ASSERT_EQ(_mkdir(dir_path.c_str()), 0);
#else
    ASSERT_EQ(mkdir(dir_path.c_str(), 0755), 0);
#endif

    EXPECT_FALSE(profiler::profiler_impl::write_file_checked(dir_path, "should not publish"));
    // The temp file (dir_path + ".tmp") must not be left behind either.
    EXPECT_FALSE(file_exists(dir_path + ".tmp"));

#if defined(_WIN32)
    _rmdir(dir_path.c_str());
#else
    rmdir(dir_path.c_str());
#endif
}

PROFILERTEST(CheckedFileWrite, overwrites_existing_valid_file_on_success)
{
    const std::string path = "checked_file_write_overwrite.txt";
    ASSERT_TRUE(profiler::profiler_impl::write_file_checked(path, "first version"));
    ASSERT_TRUE(profiler::profiler_impl::write_file_checked(path, "second version"));
    EXPECT_EQ(read_file(path), "second version");

    std::remove(path.c_str());
}
