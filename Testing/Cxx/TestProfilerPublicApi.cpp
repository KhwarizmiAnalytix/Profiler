/*
 * SPDX-License-Identifier: GPL-3.0-or-later OR Commercial
 *
 * The public product API is profiler.h only. Any external repo should be
 * able to compile against that header and produce a Chrome / Perfetto trace.
 */

#include <cstdio>
#include <string>

#include "ProfilerTest.h"
#include "profiler.h"

PROFILERTEST(PublicApi, session_from_umbrella_header)
{
    profiler::profiler_session session;
    ASSERT_TRUE(session.start());
    {
        PROFILER_PROFILE_SCOPE("external_app_scope");
        PROFILER_PROFILE_FUNCTION();
    }
    ASSERT_TRUE(session.stop());

    const std::string chrome = session.generate_chrome_trace_json();
    EXPECT_FALSE(chrome.empty());
    EXPECT_NE(chrome.find("\"traceEvents\""), std::string::npos);
    EXPECT_NE(chrome.find("external_app_scope"), std::string::npos);
}

PROFILERTEST(PublicApi, write_chrome_trace_from_umbrella_header)
{
    profiler::profiler_session session;
    ASSERT_TRUE(session.start());
    {
        PROFILER_PROFILE_SCOPE("disk_scope");
    }
    ASSERT_TRUE(session.stop());

    const std::string path = "public_api_trace.json";
    ASSERT_TRUE(session.write_chrome_trace(path));
    std::remove(path.c_str());
}

PROFILERTEST(PublicApi, start_rejects_second_session)
{
    profiler::profiler_session first;
    ASSERT_TRUE(first.start());
    profiler::profiler_session second;
    EXPECT_FALSE(second.start());
    ASSERT_TRUE(first.stop());
}
