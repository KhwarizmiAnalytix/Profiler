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

#include <gtest/gtest.h>

#include <chrono>
#include <fstream>
#include <sstream>
#include <thread>

#include "ProfilerTest.h"
#include "native/exporters/chrome_trace_exporter.h"
#include "native/session/profiler.h"

using namespace profiler;

// ============================================================================
// Chrome Trace Export with Hierarchical Profiling Tests
// ============================================================================

PROFILERTEST(Profiler, chrome_trace_uses_microseconds)
{
    x_space space;
    auto*   plane = space.add_planes();
    (*plane->mutable_event_metadata())[1].set_name("known_interval");
    auto* line = plane->add_lines();
    line->set_timestamp_ns(1000000);  // 1 ms
    auto* event = line->add_events();
    event->set_metadata_id(1);
    event->set_offset_ps(250000);     // 0.25 us
    event->set_duration_ps(2500000);  // 2.5 us

    for (const bool pretty_print : {false, true})
    {
        const auto json = profiler_impl::export_to_chrome_trace_json(space, pretty_print);
        const auto ts   = json.find("\"ts\":");
        const auto dur  = json.find("\"dur\":");
        ASSERT_NE(ts, std::string::npos);
        ASSERT_NE(dur, std::string::npos);
        EXPECT_DOUBLE_EQ(std::stod(json.substr(ts + 5)), 1000.25);
        EXPECT_DOUBLE_EQ(std::stod(json.substr(dur + 6)), 2.5);
    }
}

// Phase 6.H (design-review.md section 6.7 / Phase 6): "Treat event
// schema/version and export compatibility as contracts." This test exists
// to fail loudly if chrome_trace_exporter.h's kChromeTraceSchemaVersion
// changes without a deliberate update here -- a schema-affecting change
// should never land silently. If this test fails because the constant was
// bumped on purpose, update the expected value below (and confirm the
// bump was actually warranted by a real structural change to the emitted
// JSON, not just an unrelated edit).
PROFILERTEST(Profiler, chrome_trace_json_publishes_its_schema_version)
{
    x_space const space;  // Even an empty capture must carry the version.
    const auto    json = profiler_impl::export_to_chrome_trace_json(space);
    const auto    key  = json.find("\"profilerChromeTraceSchemaVersion\":");
    ASSERT_NE(key, std::string::npos)
        << "exported JSON is missing the schema-version field entirely";
    const auto value_start = key + std::string("\"profilerChromeTraceSchemaVersion\":").size();
    EXPECT_EQ(std::stoi(json.substr(value_start)), profiler_impl::kChromeTraceSchemaVersion);
    // Pin the actual current value too, not just self-consistency with the
    // constant -- catches the constant itself drifting unnoticed.
    EXPECT_EQ(profiler_impl::kChromeTraceSchemaVersion, 1);
}

PROFILERTEST(Profiler, chrome_trace_hierarchical_single_scope)
{
    profiler_options opts;
    auto             session = std::make_unique<profiler_session>(opts);
    EXPECT_TRUE(session != nullptr);
    EXPECT_TRUE(session->start());

    {
        profiler_scope scope("test_scope", session.get());
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    EXPECT_TRUE(session->stop());

    std::string json = session->generate_chrome_trace_json();
    EXPECT_FALSE(json.empty());
    EXPECT_NE(json.find("test_scope"), std::string::npos);
    EXPECT_NE(json.find("traceEvents"), std::string::npos);
}

PROFILERTEST(Profiler, chrome_trace_hierarchical_nested_scopes)
{
    profiler_options opts;
    auto             session = std::make_unique<profiler_session>(opts);
    EXPECT_TRUE(session != nullptr);
    EXPECT_TRUE(session->start());

    {
        profiler_scope outer("outer_scope", session.get());
        {
            profiler_scope inner("inner_scope", session.get());
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    }

    EXPECT_TRUE(session->stop());

    std::string json = session->generate_chrome_trace_json();
    EXPECT_NE(json.find("outer_scope"), std::string::npos);
    EXPECT_NE(json.find("inner_scope"), std::string::npos);
}

PROFILERTEST(Profiler, chrome_trace_hierarchical_multiple_threads)
{
    profiler_options opts;
    auto             session = std::make_unique<profiler_session>(opts);
    EXPECT_TRUE(session != nullptr);
    EXPECT_TRUE(session->start());

    std::thread t1(
        [session = session.get()]()
        {
            profiler_scope scope("thread1_scope", session);
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        });

    std::thread t2(
        [session = session.get()]()
        {
            profiler_scope scope("thread2_scope", session);
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        });

    t1.join();
    t2.join();

    EXPECT_TRUE(session->stop());

    std::string json = session->generate_chrome_trace_json();
    EXPECT_NE(json.find("thread1_scope"), std::string::npos);
    EXPECT_NE(json.find("thread2_scope"), std::string::npos);
}

PROFILERTEST(Profiler, chrome_trace_write_to_file)
{
    profiler_options opts;
    auto             session = std::make_unique<profiler_session>(opts);
    EXPECT_TRUE(session != nullptr);
    EXPECT_TRUE(session->start());

    {
        profiler_scope scope("file_test_scope", session.get());
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    EXPECT_TRUE(session->stop());

    std::string filename = "test_chrome_trace.json";
    EXPECT_TRUE(session->write_chrome_trace(filename));

    // Verify file exists and contains valid JSON
    std::ifstream file(filename);
    EXPECT_TRUE(file.good());

    std::stringstream buffer;
    buffer << file.rdbuf();
    std::string content = buffer.str();

    EXPECT_FALSE(content.empty());
    EXPECT_NE(content.find("traceEvents"), std::string::npos);
    EXPECT_NE(content.find("file_test_scope"), std::string::npos);

    // Clean up
    std::remove(filename.c_str());
}

PROFILERTEST(Profiler, chrome_trace_hierarchical_deep_nesting)
{
    profiler_options opts;
    auto             session = std::make_unique<profiler_session>(opts);
    EXPECT_TRUE(session != nullptr);
    EXPECT_TRUE(session->start());

    {
        profiler_scope level1("level1", session.get());
        {
            profiler_scope level2("level2", session.get());
            {
                profiler_scope level3("level3", session.get());
                {
                    profiler_scope level4("level4", session.get());
                    std::this_thread::sleep_for(std::chrono::milliseconds(1));
                }
            }
        }
    }

    EXPECT_TRUE(session->stop());

    std::string json = session->generate_chrome_trace_json();
    EXPECT_NE(json.find("level1"), std::string::npos);
    EXPECT_NE(json.find("level2"), std::string::npos);
    EXPECT_NE(json.find("level3"), std::string::npos);
    EXPECT_NE(json.find("level4"), std::string::npos);
}

PROFILERTEST(Profiler, chrome_trace_hierarchical_sibling_scopes)
{
    profiler_options opts;
    auto             session = std::make_unique<profiler_session>(opts);
    EXPECT_TRUE(session != nullptr);
    EXPECT_TRUE(session->start());

    {
        profiler_scope parent("parent", session.get());
        {
            profiler_scope child1("child1", session.get());
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        {
            profiler_scope child2("child2", session.get());
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    }

    EXPECT_TRUE(session->stop());

    std::string json = session->generate_chrome_trace_json();
    EXPECT_NE(json.find("parent"), std::string::npos);
    EXPECT_NE(json.find("child1"), std::string::npos);
    EXPECT_NE(json.find("child2"), std::string::npos);
}

PROFILERTEST(Profiler, chrome_trace_json_format_validation)
{
    profiler_options opts;
    auto             session = std::make_unique<profiler_session>(opts);
    EXPECT_TRUE(session != nullptr);
    EXPECT_TRUE(session->start());

    {
        profiler_scope scope("format_test", session.get());
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    EXPECT_TRUE(session->stop());

    std::string json = session->generate_chrome_trace_json();

    // Verify required Chrome Trace Event Format fields
    EXPECT_NE(json.find("\"displayTimeUnit\""), std::string::npos);
    EXPECT_NE(json.find("\"ns\""), std::string::npos);
    EXPECT_NE(json.find("\"traceEvents\""), std::string::npos);
    EXPECT_NE(json.find("\"ph\""), std::string::npos);
    EXPECT_NE(json.find("\"pid\""), std::string::npos);
    EXPECT_NE(json.find("\"tid\""), std::string::npos);
    EXPECT_NE(json.find("\"ts\""), std::string::npos);
    EXPECT_NE(json.find("\"dur\""), std::string::npos);
}

PROFILERTEST(Profiler, chrome_trace_empty_session)
{
    profiler_options opts;
    auto             session = std::make_unique<profiler_session>(opts);
    EXPECT_TRUE(session != nullptr);
    EXPECT_TRUE(session->start());
    EXPECT_TRUE(session->stop());

    std::string json = session->generate_chrome_trace_json();
    EXPECT_FALSE(json.empty());
    EXPECT_NE(json.find("traceEvents"), std::string::npos);
}

PROFILERTEST(Profiler, chrome_trace_scope_with_special_characters)
{
    profiler_options opts;
    auto             session = std::make_unique<profiler_session>(opts);
    EXPECT_TRUE(session != nullptr);
    EXPECT_TRUE(session->start());

    {
        profiler_scope scope("scope_with_\"quotes\"", session.get());
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    EXPECT_TRUE(session->stop());

    std::string json = session->generate_chrome_trace_json();
    EXPECT_FALSE(json.empty());
    // Should contain escaped quotes
    EXPECT_NE(json.find("\\\""), std::string::npos);
}
