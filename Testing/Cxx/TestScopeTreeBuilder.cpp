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

// Unit tests for native/session/scope_tree_builder.h, built directly against a
// hand-assembled XSpace (via xplane_builder) rather than a live profiler_session,
// so interval-containment nesting can be verified against known timestamps.

#include <chrono>
#include <cstdint>
#include <string>

#include "ProfilerTest.h"
#include "native/core/timespan.h"
#include "native/exporters/xplane/xplane.h"
#include "native/exporters/xplane/xplane_builder.h"
#include "native/exporters/xplane/xplane_schema.h"
#include "native/session/profiler.h"
#include "native/session/scope_tree_builder.h"

namespace
{
void add_duration_event(profiler::xplane_builder& builder,
    int64_t                                       line_id,
    const char*                                   name,
    uint64_t                                      begin_ps,
    uint64_t                                      duration_ps)
{
    auto  line     = builder.get_or_create_line(line_id);
    auto* metadata = builder.get_or_create_event_metadata(name);
    line.add_event(profiler::timespan(begin_ps, duration_ps), *metadata);
}

int64_t duration_ns_of(const profiler::profiler_scope_data& node)
{
    return std::chrono::duration_cast<std::chrono::nanoseconds>(node.end_time_ - node.start_time_)
        .count();
}
}  // namespace

PROFILERTEST(ScopeTreeBuilder, empty_xspace_returns_null)
{
    profiler::x_space space;
    EXPECT_EQ(profiler::scope_tree_builder::build_scope_tree(space), nullptr);
}

PROFILERTEST(ScopeTreeBuilder, ignores_non_host_thread_planes)
{
    profiler::x_space space;
    profiler::xplane* plane = space.add_planes();
    plane->set_name("/device:GPU:0");  // Not the host-threads plane.
    profiler::xplane_builder builder(plane);
    add_duration_event(builder, /*line_id=*/1, "kernel", 0, 1000);

    EXPECT_EQ(profiler::scope_tree_builder::build_scope_tree(space), nullptr);
}

PROFILERTEST(ScopeTreeBuilder, single_event_becomes_direct_child_of_root)
{
    profiler::x_space space;
    profiler::xplane* plane = space.add_planes();
    plane->set_name(std::string(profiler::kHostThreadsPlaneName));
    profiler::xplane_builder builder(plane);
    add_duration_event(builder, 1, "workload", /*begin_ps=*/0, /*duration_ps=*/10000);  // 0..10 ns

    auto root = profiler::scope_tree_builder::build_scope_tree(space);
    ASSERT_NE(root, nullptr);
    EXPECT_EQ(root->name_, "ROOT");
    ASSERT_EQ(root->children_.size(), 1u);
    EXPECT_EQ(root->children_[0]->name_, "workload");
    EXPECT_EQ(root->children_[0]->depth_level_, 1u);
    EXPECT_EQ(duration_ns_of(*root->children_[0]), 10);
}

PROFILERTEST(ScopeTreeBuilder, nested_events_become_children_by_interval_containment)
{
    profiler::x_space space;
    profiler::xplane* plane = space.add_planes();
    plane->set_name(std::string(profiler::kHostThreadsPlaneName));
    profiler::xplane_builder builder(plane);

    // outer: [0, 10] ns; inner: [2, 6] ns (nested inside outer);
    // sibling: [12, 14] ns (starts after outer ends -> a second root child, not nested).
    add_duration_event(builder, 1, "outer", 0, 10000);
    add_duration_event(builder, 1, "inner", 2000, 4000);
    add_duration_event(builder, 1, "sibling", 12000, 2000);

    auto root = profiler::scope_tree_builder::build_scope_tree(space);
    ASSERT_NE(root, nullptr);
    ASSERT_EQ(root->children_.size(), 2u);

    const auto* outer = root->children_[0].get();
    EXPECT_EQ(outer->name_, "outer");
    EXPECT_EQ(outer->depth_level_, 1u);
    ASSERT_EQ(outer->children_.size(), 1u);
    EXPECT_EQ(outer->children_[0]->name_, "inner");
    EXPECT_EQ(outer->children_[0]->depth_level_, 2u);

    const auto* sibling = root->children_[1].get();
    EXPECT_EQ(sibling->name_, "sibling");
    EXPECT_EQ(sibling->depth_level_, 1u);
    EXPECT_TRUE(sibling->children_.empty());
}

// Regression for the false-parenting defect in docs/design-review.md finding 8:
// A=[1000,3000) starts before B=[2000,4000) and B starts inside A's interval, but
// B ends after A closes -- they overlap without either containing the other, so
// neither may become the other's parent.
PROFILERTEST(ScopeTreeBuilder, crossing_intervals_do_not_nest)
{
    profiler::x_space space;
    profiler::xplane* plane = space.add_planes();
    plane->set_name(std::string(profiler::kHostThreadsPlaneName));
    profiler::xplane_builder builder(plane);

    add_duration_event(builder, 1, "A", 1000, 2000);  // [1000, 3000) ns
    add_duration_event(builder, 1, "B", 2000, 2000);  // [2000, 4000) ns

    auto root = profiler::scope_tree_builder::build_scope_tree(space);
    ASSERT_NE(root, nullptr);
    ASSERT_EQ(root->children_.size(), 2u);

    EXPECT_EQ(root->children_[0]->name_, "A");
    EXPECT_TRUE(root->children_[0]->children_.empty());
    EXPECT_EQ(root->children_[1]->name_, "B");
    EXPECT_TRUE(root->children_[1]->children_.empty());
}

PROFILERTEST(ScopeTreeBuilder, separate_lines_produce_separate_thread_labeled_branches)
{
    profiler::x_space space;
    profiler::xplane* plane = space.add_planes();
    plane->set_name(std::string(profiler::kHostThreadsPlaneName));
    profiler::xplane_builder builder(plane);

    add_duration_event(builder, /*line_id=*/1, "thread_a_scope", 0, 1000);
    add_duration_event(builder, /*line_id=*/2, "thread_b_scope", 0, 1000);
    builder.get_or_create_line(2).SetName("worker_b");  // line 1 keeps the default id-based label.

    auto root = profiler::scope_tree_builder::build_scope_tree(space);
    ASSERT_NE(root, nullptr);
    ASSERT_EQ(root->children_.size(), 2u);

    bool found_a = false;
    bool found_b = false;
    for (const auto& child : root->children_)
    {
        if (child->name_ == "thread_a_scope")
        {
            found_a = true;
            EXPECT_EQ(child->thread_label_, "thread 1");
        }
        else if (child->name_ == "thread_b_scope")
        {
            found_b = true;
            EXPECT_EQ(child->thread_label_, "worker_b");
        }
    }
    EXPECT_TRUE(found_a);
    EXPECT_TRUE(found_b);
}
