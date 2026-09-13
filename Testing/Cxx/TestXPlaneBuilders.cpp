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

// Unit tests for the XSpace builder layer (native/exporters/xplane/xplane_builder.h
// and xplane.h): the simple_atod/simple_atoi parsers, and the xplane/xline/xevent
// builder round trip. This complements TestProfilerXPlanePipeline.cpp, which only
// exercises the visitor side through a real profiler_session.

#include <cstdint>
#include <string>

#include "ProfilerTest.h"
#include "native/core/timespan.h"
#include "native/exporters/xplane/xplane.h"
#include "native/exporters/xplane/xplane_builder.h"

// ---------------------------------------------------------------------------
// simple_atod
// ---------------------------------------------------------------------------

PROFILERTEST(SimpleAtod, parses_integers_decimals_and_signs)
{
    double value = 0.0;
    ASSERT_TRUE(profiler::simple_atod("42", &value));
    EXPECT_NEAR(value, 42.0, 1e-9);

    ASSERT_TRUE(profiler::simple_atod("3.14", &value));
    EXPECT_NEAR(value, 3.14, 1e-9);

    ASSERT_TRUE(profiler::simple_atod("-2.5", &value));
    EXPECT_NEAR(value, -2.5, 1e-9);

    ASSERT_TRUE(profiler::simple_atod("+7", &value));
    EXPECT_NEAR(value, 7.0, 1e-9);
}

PROFILERTEST(SimpleAtod, parses_scientific_notation)
{
    double value = 0.0;
    ASSERT_TRUE(profiler::simple_atod("1.5e2", &value));
    EXPECT_NEAR(value, 150.0, 1e-9);

    ASSERT_TRUE(profiler::simple_atod("1.5e-2", &value));
    EXPECT_NEAR(value, 0.015, 1e-9);
}

PROFILERTEST(SimpleAtod, rejects_malformed_input)
{
    double value = 0.0;
    EXPECT_FALSE(profiler::simple_atod("", &value));
    EXPECT_FALSE(profiler::simple_atod("12.3.4", &value));
    EXPECT_FALSE(profiler::simple_atod("12abc", &value));
    EXPECT_FALSE(profiler::simple_atod("abc", &value));
    EXPECT_FALSE(profiler::simple_atod("1.0", nullptr));
}

// ---------------------------------------------------------------------------
// simple_atoi
// ---------------------------------------------------------------------------

PROFILERTEST(SimpleAtoi, parses_signed_and_unsigned_integers)
{
    int64_t  signed_value   = 0;
    uint64_t unsigned_value = 0;
    ASSERT_TRUE(profiler::simple_atoi("123", &signed_value));
    EXPECT_EQ(signed_value, 123);

    ASSERT_TRUE(profiler::simple_atoi("-123", &signed_value));
    EXPECT_EQ(signed_value, -123);

    ASSERT_TRUE(profiler::simple_atoi("456", &unsigned_value));
    EXPECT_EQ(unsigned_value, 456u);
}

PROFILERTEST(SimpleAtoi, rejects_non_numeric_and_empty_input)
{
    int64_t value = 0;
    EXPECT_FALSE(profiler::simple_atoi("", &value));
    EXPECT_FALSE(profiler::simple_atoi("12.5", &value));
    EXPECT_FALSE(profiler::simple_atoi("abc", &value));
    EXPECT_FALSE(profiler::simple_atoi("12abc", &value));
}

PROFILERTEST(SimpleAtoi, rejects_out_of_range_values)
{
    // One digit past int8_t's range in each direction.
    int8_t small_value = 0;
    EXPECT_FALSE(profiler::simple_atoi("200", &small_value));
    EXPECT_FALSE(profiler::simple_atoi("-200", &small_value));
    EXPECT_TRUE(profiler::simple_atoi("100", &small_value));
    EXPECT_EQ(small_value, 100);
}

// ---------------------------------------------------------------------------
// xplane_builder / xline_builder / xevent_builder round trip
// ---------------------------------------------------------------------------

PROFILERTEST(XPlaneBuilder, get_or_create_event_metadata_is_idempotent_by_name)
{
    profiler::xplane         plane;
    profiler::xplane_builder builder(&plane);

    auto* first  = builder.get_or_create_event_metadata("matmul");
    auto* second = builder.get_or_create_event_metadata("matmul");
    EXPECT_EQ(first, second);
    EXPECT_EQ(first->name(), "matmul");

    auto* other = builder.get_or_create_event_metadata("conv2d");
    EXPECT_NE(other->id(), first->id());
}

PROFILERTEST(XPlaneBuilder, get_or_create_line_reuses_line_for_same_id)
{
    profiler::xplane         plane;
    profiler::xplane_builder builder(&plane);

    auto first = builder.get_or_create_line(/*line_id=*/42);
    first.SetName("worker");
    auto second = builder.get_or_create_line(/*line_id=*/42);
    EXPECT_EQ(second.Name(), "worker");
    EXPECT_EQ(plane.lines_size(), 1u);
}

PROFILERTEST(XEventBuilder, add_event_sets_timestamp_and_duration_from_timespan)
{
    profiler::xplane         plane;
    profiler::xplane_builder builder(&plane);
    auto                     line     = builder.get_or_create_line(1);
    auto*                    metadata = builder.get_or_create_event_metadata("compute");

    auto event =
        line.add_event(profiler::timespan(/*begin_ps=*/2000, /*duration_ps=*/5000), *metadata);
    EXPECT_EQ(event.MetadataId(), metadata->id());
    EXPECT_EQ(event.OffsetPs(), 2000);
    EXPECT_EQ(event.DurationPs(), 5000);
    EXPECT_EQ(event.TimestampPs(), 2000);  // Line timestamp defaults to 0.

    event.SetDurationNs(10);
    EXPECT_EQ(event.DurationPs(), 10000);
}

PROFILERTEST(XStatsBuilder, add_and_retrieve_typed_stat_values)
{
    profiler::xplane         plane;
    profiler::xplane_builder builder(&plane);
    auto                     line     = builder.get_or_create_line(1);
    auto*                    metadata = builder.get_or_create_event_metadata("op");
    auto                     event    = line.add_event(*metadata);

    auto* count_stat_meta = builder.get_or_create_stat_metadata("count");
    event.add_stat_value(*count_stat_meta, int64_t{7});

    const auto* stat = event.GetStat(*count_stat_meta);
    ASSERT_NE(stat, nullptr);
    EXPECT_EQ(stat->value_case(), profiler::xstat::value_case_type::kInt64Value);
    EXPECT_EQ(profiler::xstats_builder<profiler::xevent>::IntOrUintValue(*stat), 7u);
}

PROFILERTEST(XStatsBuilder, parse_and_add_stat_value_infers_the_narrowest_matching_type)
{
    profiler::xplane         plane;
    profiler::xplane_builder builder(&plane);
    auto                     line     = builder.get_or_create_line(1);
    auto*                    metadata = builder.get_or_create_event_metadata("op");
    auto                     event    = line.add_event(*metadata);

    auto* int_meta    = builder.get_or_create_stat_metadata("int_field");
    auto* double_meta = builder.get_or_create_stat_metadata("double_field");
    auto* str_meta    = builder.get_or_create_stat_metadata("str_field");

    event.parse_and_add_stat_value(*int_meta, "42");
    event.parse_and_add_stat_value(*double_meta, "3.14");
    event.parse_and_add_stat_value(*str_meta, "not_a_number");

    EXPECT_EQ(
        event.GetStat(*int_meta)->value_case(), profiler::xstat::value_case_type::kInt64Value);
    EXPECT_EQ(event.GetStat(*int_meta)->int64_value(), 42);

    EXPECT_EQ(
        event.GetStat(*double_meta)->value_case(), profiler::xstat::value_case_type::kDoubleValue);
    EXPECT_NEAR(event.GetStat(*double_meta)->double_value(), 3.14, 1e-9);

    // Non-numeric values are stored as a reference to interned stat metadata.
    EXPECT_EQ(event.GetStat(*str_meta)->value_case(), profiler::xstat::value_case_type::kRefValue);
}
