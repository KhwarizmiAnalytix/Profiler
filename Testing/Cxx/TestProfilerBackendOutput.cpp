/*
 * XSigma: High-Performance Computational Library
 *
 * SPDX-License-Identifier: GPL-3.0-or-later OR Commercial
 *
 * This file is part of XSigma and is licensed under a dual-license model:
 *
 *   - Open-source License (GPLv3):
 *       Free for personal, academic, and research use under the terms of
 *       the GNU General Public License v3.0 or later.
 *
 *   - Commercial License:
 *       A commercial license is required for proprietary, closed-source,
 *       or SaaS usage. Contact us to obtain a commercial agreement.
 *
 * Contact: licensing@xsigma.co.uk
 * Website: https://www.xsigma.co.uk
 */

/*
 * =============================================================================
 * Native profiler OUTPUT use cases
 * =============================================================================
 *
 * The native session has two independent output families:
 *
 *   A) Chrome Trace (timeline)
 *      - generate_chrome_trace_json() / write_chrome_trace(path)
 *      - Source: XSpace collected by host_tracer (TraceMe intervals)
 *      - Consumers: chrome://tracing, Perfetto UI
 *      - NOT selected via output_format_; it is a separate API
 *
 *   B) Structured reports (summaries / hierarchies)
 *      - generate_report() → profiler_report::{console,json,csv,xml}
 *      - export_report(path) uses options_.output_format_
 *      - Source: scope_tree_builder reconstructs hierarchy from XSpace, then
 *        joins timing/memory samples from statistical_analyzer / memory_tracker
 *
 * output_format_enum mapping (export_to_file):
 *   CONSOLE     → human-readable text (stdout use via print_report)
 *   FILE        → same text content as CONSOLE, written to a path
 *   JSON        → programmatic report (header, scopes, memory, threads)
 *   CSV         → flat spreadsheet rows (Scope,Depth,Thread,Duration,...)
 *   STRUCTURED  → XML wrapper around the same sections as console
 *
 * This file exercises every format after one shared nested workload so each
 * use case is documented next to the assertions that prove it works.
 */

#include <cstdio>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

#include "ProfilerTest.h"
#include "native/session/profiler.h"
#include "native/session/profiler_report.h"

using namespace profiler;

namespace
{

constexpr const char* kOuterScope = "output_outer_scope";
constexpr const char* kInnerScope = "output_inner_scope";

// Shared nested workload: outer → inner. Enough TraceMe events for both Chrome
// timelines and scope_tree reconstruction used by reports.
void run_nested_workload(profiler_session& session)
{
    {
        profiler_scope outer(kOuterScope, &session);
        {
            profiler_scope  inner(kInnerScope, &session);
            volatile double sink = 0.0;
            for (int i = 0; i < 1000; ++i)
            {
                sink += static_cast<double>(i) * 0.001;
            }
            (void)sink;
        }
    }
}

profiler_options make_report_options(profiler_options::output_format_enum format)
{
    profiler_options opts;
    opts.enable_timing_                 = true;
    opts.enable_memory_tracking_        = true;
    opts.enable_hierarchical_profiling_ = true;
    opts.enable_statistical_analysis_   = true;
    opts.track_memory_deltas_           = true;
    opts.track_peak_memory_             = true;
    opts.output_format_                 = format;
    return opts;
}

std::string read_file(const std::string& path)
{
    std::ifstream in(path);
    if (!in.good())
    {
        return {};
    }
    return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

}  // namespace

// -----------------------------------------------------------------------------
// Use case: Chrome Trace JSON in memory
// Audience: interactive timeline viewers (chrome://tracing, Perfetto).
// -----------------------------------------------------------------------------
PROFILERTEST(BackendOutput, chrome_trace_json_in_memory)
{
    profiler_session session(make_report_options(profiler_options::output_format_enum::JSON));
    ASSERT_TRUE(session.start());
    run_nested_workload(session);
    ASSERT_TRUE(session.stop());

    // Chrome JSON is produced from XSpace, independent of output_format_.
    const std::string chrome = session.generate_chrome_trace_json();
    EXPECT_FALSE(chrome.empty());
    EXPECT_NE(chrome.find("\"traceEvents\""), std::string::npos)
        << "Chrome Trace Event Format requires a top-level traceEvents array";
    EXPECT_NE(chrome.find(kOuterScope), std::string::npos);
    EXPECT_NE(chrome.find(kInnerScope), std::string::npos);
}

// -----------------------------------------------------------------------------
// Use case: Chrome Trace written to disk
// Audience: offline analysis; same schema as in-memory Chrome JSON.
// -----------------------------------------------------------------------------
PROFILERTEST(BackendOutput, chrome_trace_write_to_file)
{
    profiler_session session(make_report_options(profiler_options::output_format_enum::CONSOLE));
    ASSERT_TRUE(session.start());
    run_nested_workload(session);
    ASSERT_TRUE(session.stop());

    const std::string path = "backend_output_chrome_trace.json";
    ASSERT_TRUE(session.write_chrome_trace(path));

    const std::string content = read_file(path);
    EXPECT_FALSE(content.empty());
    EXPECT_NE(content.find("\"traceEvents\""), std::string::npos);
    EXPECT_NE(content.find(kOuterScope), std::string::npos);
    std::remove(path.c_str());
}

// -----------------------------------------------------------------------------
// Use case: CONSOLE report (human-readable)
// Audience: quick terminal inspection after a session.
// Sections: header, summary, hierarchy, timing, memory, statistics[, threads].
// -----------------------------------------------------------------------------
PROFILERTEST(BackendOutput, console_report_text)
{
    profiler_session session(make_report_options(profiler_options::output_format_enum::CONSOLE));
    ASSERT_TRUE(session.start());
    run_nested_workload(session);
    ASSERT_TRUE(session.stop());

    auto report = session.generate_report();
    ASSERT_NE(report, nullptr);

    const std::string text = report->generate_console_report();
    EXPECT_FALSE(text.empty());
    EXPECT_NE(text.find("Profiler Profiler Report"), std::string::npos);
    EXPECT_NE(text.find(kOuterScope), std::string::npos);
    EXPECT_NE(text.find(kInnerScope), std::string::npos);
    // stats_calculator offline summary from collected_xspace (enable_statistical_analysis_).
    EXPECT_NE(text.find("=== Statistical Analysis ==="), std::string::npos);
    EXPECT_NE(text.find("=== Node Stats (from XSpace) ==="), std::string::npos);
    EXPECT_NE(text.find("nodes observed"), std::string::npos);
    EXPECT_NE(text.find("Summary by node type"), std::string::npos);

    std::cout << "\n=== BackendOutput console report ===\n" << text << std::flush;
    session.print_report();
}

// -----------------------------------------------------------------------------
// Use case: FILE format
// Audience: save the same console text to a path without changing content.
// Note: FILE and CONSOLE share generate_console_report() in export_to_file().
// -----------------------------------------------------------------------------
PROFILERTEST(BackendOutput, file_format_exports_console_text)
{
    profiler_options opts  = make_report_options(profiler_options::output_format_enum::FILE);
    opts.output_file_path_ = "backend_output_file_report.txt";

    profiler_session session(opts);
    ASSERT_TRUE(session.start());
    run_nested_workload(session);
    ASSERT_TRUE(session.stop());

    const std::string path = opts.output_file_path_;
    session.export_report(path);

    const std::string content = read_file(path);
    EXPECT_FALSE(content.empty());
    EXPECT_NE(content.find("Profiler Profiler Report"), std::string::npos);
    EXPECT_NE(content.find(kOuterScope), std::string::npos);
    std::remove(path.c_str());
}

// -----------------------------------------------------------------------------
// Use case: JSON structured report
// Audience: tools / scripts that parse session summaries (not Chrome schema).
// Keys: header, scopes, top_durations, memory, threads.
// Builds scope_tree from XSpace (also covers scope_tree_builder).
// -----------------------------------------------------------------------------
PROFILERTEST(BackendOutput, json_report_structure)
{
    profiler_session session(make_report_options(profiler_options::output_format_enum::JSON));
    ASSERT_TRUE(session.start());
    run_nested_workload(session);
    ASSERT_TRUE(session.stop());

    auto report = session.generate_report();
    ASSERT_NE(report, nullptr);

    const std::string json = report->generate_json_report();
    EXPECT_FALSE(json.empty());
    EXPECT_NE(json.find("\"header\""), std::string::npos);
    EXPECT_NE(json.find("\"scopes\""), std::string::npos);
    EXPECT_NE(json.find("\"top_durations\""), std::string::npos);
    EXPECT_NE(json.find("\"memory\""), std::string::npos);
    EXPECT_NE(json.find(kOuterScope), std::string::npos);
    EXPECT_NE(json.find(kInnerScope), std::string::npos);

    // Session-level export_report must honor output_format_ == JSON.
    const std::string path = "backend_output_report.json";
    session.export_report(path);
    const std::string on_disk = read_file(path);
    EXPECT_NE(on_disk.find("\"header\""), std::string::npos);
    std::remove(path.c_str());
}

// -----------------------------------------------------------------------------
// Use case: CSV report
// Audience: spreadsheets / dataframes; one row per scope in the tree.
// Header: Scope,Depth,Thread,Duration(ms),Memory Delta Mean,Memory Delta Max
// -----------------------------------------------------------------------------
PROFILERTEST(BackendOutput, csv_report_rows)
{
    profiler_session session(make_report_options(profiler_options::output_format_enum::CSV));
    ASSERT_TRUE(session.start());
    run_nested_workload(session);
    ASSERT_TRUE(session.stop());

    auto report = session.generate_report();
    ASSERT_NE(report, nullptr);

    const std::string csv = report->generate_csv_report();
    EXPECT_FALSE(csv.empty());
    EXPECT_NE(
        csv.find("Scope,Depth,Thread,Duration(ms),Memory Delta Mean,Memory Delta Max"),
        std::string::npos);
    EXPECT_NE(csv.find(kOuterScope), std::string::npos);
    EXPECT_NE(csv.find(kInnerScope), std::string::npos);

    const std::string path = "backend_output_report.csv";
    EXPECT_TRUE(report->export_csv_report(path));
    EXPECT_FALSE(read_file(path).empty());
    std::remove(path.c_str());
}

// -----------------------------------------------------------------------------
// Use case: STRUCTURED (XML) report
// Audience: systems that prefer XML envelopes; same sections as console text
// wrapped in <profiler_report> with header/summary/timing/memory/statistics.
// -----------------------------------------------------------------------------
PROFILERTEST(BackendOutput, structured_xml_report)
{
    profiler_session session(make_report_options(profiler_options::output_format_enum::STRUCTURED));
    ASSERT_TRUE(session.start());
    run_nested_workload(session);
    ASSERT_TRUE(session.stop());

    auto report = session.generate_report();
    ASSERT_NE(report, nullptr);

    const std::string xml = report->generate_xml_report();
    EXPECT_FALSE(xml.empty());
    EXPECT_NE(xml.find("<?xml version=\"1.0\""), std::string::npos);
    EXPECT_NE(xml.find("<profiler_report>"), std::string::npos);
    EXPECT_NE(xml.find("</profiler_report>"), std::string::npos);
    EXPECT_NE(xml.find("<timing>"), std::string::npos);
    EXPECT_NE(xml.find("<memory>"), std::string::npos);
    EXPECT_NE(xml.find(kOuterScope), std::string::npos);

    const std::string path = "backend_output_report.xml";
    session.export_report(path);
    const std::string on_disk = read_file(path);
    EXPECT_NE(on_disk.find("<profiler_report>"), std::string::npos);
    std::remove(path.c_str());
}

// -----------------------------------------------------------------------------
// Use case: all formats from one stopped session
// Ensures generate_report() can emit every string format without re-profiling,
// which is the typical "stop once, export many" workflow.
// -----------------------------------------------------------------------------
PROFILERTEST(BackendOutput, all_formats_from_one_session)
{
    profiler_session session(make_report_options(profiler_options::output_format_enum::JSON));
    ASSERT_TRUE(session.start());
    run_nested_workload(session);
    ASSERT_TRUE(session.stop());
    ASSERT_TRUE(session.has_collected_xspace());

    auto report = session.generate_report();
    ASSERT_NE(report, nullptr);

    const std::string console = report->generate_console_report();
    const std::string json    = report->generate_json_report();
    const std::string csv     = report->generate_csv_report();
    const std::string xml     = report->generate_xml_report();
    const std::string chrome  = session.generate_chrome_trace_json();

    EXPECT_FALSE(console.empty());
    EXPECT_FALSE(json.empty());
    EXPECT_FALSE(csv.empty());
    EXPECT_FALSE(xml.empty());
    EXPECT_FALSE(chrome.empty());

    // Chrome timeline and JSON report are different schemas for the same session.
    EXPECT_NE(chrome.find("\"traceEvents\""), std::string::npos);
    EXPECT_EQ(json.find("\"traceEvents\""), std::string::npos)
        << "Session JSON report must not be confused with Chrome Trace JSON";
    EXPECT_NE(json.find("\"scopes\""), std::string::npos);

    // Console + XML share generate_statistical_section (stats_calculator from XSpace).
    EXPECT_NE(console.find("=== Node Stats (from XSpace) ==="), std::string::npos);
    EXPECT_NE(console.find(kOuterScope), std::string::npos);
    EXPECT_NE(xml.find("=== Node Stats (from XSpace) ==="), std::string::npos);
    EXPECT_NE(xml.find("<statistics>"), std::string::npos);
}
