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

// Unit tests for the process-wide profiler lock (native/core/profiler_lock.h)
// and the environment-variable parsing helpers (native/platform/env_var.h).
// Both are otherwise only exercised indirectly through session start/stop.

#include <atomic>
#include <cstdlib>
#include <optional>
#include <string>
#include <thread>
#include <vector>

#include "ProfilerTest.h"
#include "native/core/profiler_lock.h"
#include "native/platform/env_var.h"

namespace
{
void SetTestEnvVar(const char* name, const char* value)
{
#if defined(_WIN32)
    _putenv_s(name, value);
#else
    setenv(name, value, 1);
#endif
}

void UnsetTestEnvVar(const char* name)
{
#if defined(_WIN32)
    _putenv_s(name, "");
#else
    unsetenv(name);
#endif
}
}  // namespace

// ---------------------------------------------------------------------------
// ProfilerLock
// ---------------------------------------------------------------------------

PROFILERTEST(ProfilerLock, exclusive_acquire_and_release)
{
    ASSERT_FALSE(profiler::ProfilerLock::HasActiveSession());

    auto first = profiler::ProfilerLock::Acquire();
    ASSERT_TRUE(first.has_value());
    EXPECT_TRUE(first->Active());
    EXPECT_TRUE(profiler::ProfilerLock::HasActiveSession());

    auto second = profiler::ProfilerLock::Acquire();
    EXPECT_FALSE(second.has_value());  // Contention: only one holder at a time.

    first->ReleaseIfActive();
    EXPECT_FALSE(profiler::ProfilerLock::HasActiveSession());

    auto third = profiler::ProfilerLock::Acquire();
    ASSERT_TRUE(third.has_value());
    third->ReleaseIfActive();
}

PROFILERTEST(ProfilerLock, move_transfers_active_ownership)
{
    auto lock = profiler::ProfilerLock::Acquire();
    ASSERT_TRUE(lock.has_value());

    profiler::ProfilerLock moved(std::move(*lock));
    EXPECT_FALSE(lock->Active());  // NOLINT(bugprone-use-after-move)
    EXPECT_TRUE(moved.Active());
    EXPECT_TRUE(profiler::ProfilerLock::HasActiveSession());

    moved.ReleaseIfActive();
    EXPECT_FALSE(profiler::ProfilerLock::HasActiveSession());
}

PROFILERTEST(ProfilerLock, destructor_releases_active_lock)
{
    {
        auto lock = profiler::ProfilerLock::Acquire();
        ASSERT_TRUE(lock.has_value());
        EXPECT_TRUE(profiler::ProfilerLock::HasActiveSession());
    }  // `lock` destroyed here; RAII should release it.
    EXPECT_FALSE(profiler::ProfilerLock::HasActiveSession());
}

PROFILERTEST(ProfilerLock, concurrent_acquire_admits_exactly_one_thread)
{
    constexpr int                                      kThreads = 8;
    std::atomic<int>                                   successes{0};
    std::vector<std::thread>                           threads;
    std::vector<std::optional<profiler::ProfilerLock>> locks(kThreads);

    for (int i = 0; i < kThreads; ++i)
    {
        threads.emplace_back(
            [&, i]
            {
                locks[i] = profiler::ProfilerLock::Acquire();
                if (locks[i].has_value())
                {
                    successes.fetch_add(1, std::memory_order_relaxed);
                }
            });
    }
    for (auto& t : threads)
    {
        t.join();
    }

    EXPECT_EQ(successes.load(), 1);
    for (auto& lock : locks)
    {
        if (lock.has_value())
        {
            lock->ReleaseIfActive();
        }
    }
    EXPECT_FALSE(profiler::ProfilerLock::HasActiveSession());
}

// ---------------------------------------------------------------------------
// env_var
// ---------------------------------------------------------------------------

PROFILERTEST(EnvVar, read_bool_uses_default_when_unset)
{
    UnsetTestEnvVar("PROFILER_TEST_BOOL_VAR");
    bool value = false;
    EXPECT_TRUE(profiler::read_bool_from_env_var("PROFILER_TEST_BOOL_VAR", true, &value));
    EXPECT_TRUE(value);
}

PROFILERTEST(EnvVar, read_bool_parses_common_true_false_spellings)
{
    bool value = false;
    SetTestEnvVar("PROFILER_TEST_BOOL_VAR", "1");
    EXPECT_TRUE(profiler::read_bool_from_env_var("PROFILER_TEST_BOOL_VAR", false, &value));
    EXPECT_TRUE(value);

    SetTestEnvVar("PROFILER_TEST_BOOL_VAR", "FALSE");
    EXPECT_TRUE(profiler::read_bool_from_env_var("PROFILER_TEST_BOOL_VAR", true, &value));
    EXPECT_FALSE(value);

    UnsetTestEnvVar("PROFILER_TEST_BOOL_VAR");
}

PROFILERTEST(EnvVar, read_bool_rejects_invalid_value_and_keeps_default)
{
    SetTestEnvVar("PROFILER_TEST_BOOL_VAR", "not_a_bool");
    bool value = true;
    EXPECT_FALSE(profiler::read_bool_from_env_var("PROFILER_TEST_BOOL_VAR", false, &value));
    EXPECT_FALSE(value);  // *value is reset to default_val before parsing fails.
    UnsetTestEnvVar("PROFILER_TEST_BOOL_VAR");
}

PROFILERTEST(EnvVar, read_int64_parses_and_trims_whitespace)
{
    SetTestEnvVar("PROFILER_TEST_INT_VAR", "  42  ");
    int64_t value = 0;
    EXPECT_TRUE(profiler::read_int64_from_env_var("PROFILER_TEST_INT_VAR", -1, &value));
    EXPECT_EQ(value, 42);
    UnsetTestEnvVar("PROFILER_TEST_INT_VAR");
}

PROFILERTEST(EnvVar, read_int64_uses_default_when_unset_and_rejects_garbage)
{
    UnsetTestEnvVar("PROFILER_TEST_INT_VAR");
    int64_t value = 0;
    EXPECT_TRUE(profiler::read_int64_from_env_var("PROFILER_TEST_INT_VAR", 7, &value));
    EXPECT_EQ(value, 7);

    SetTestEnvVar("PROFILER_TEST_INT_VAR", "not_a_number");
    EXPECT_FALSE(profiler::read_int64_from_env_var("PROFILER_TEST_INT_VAR", 7, &value));
    EXPECT_EQ(value, 7);
    UnsetTestEnvVar("PROFILER_TEST_INT_VAR");
}

PROFILERTEST(EnvVar, read_float_parses_decimal_value)
{
    SetTestEnvVar("PROFILER_TEST_FLOAT_VAR", "3.5");
    float value = 0.0F;
    EXPECT_TRUE(profiler::read_float_from_env_var("PROFILER_TEST_FLOAT_VAR", 0.0F, &value));
    EXPECT_NEAR(value, 3.5F, 1e-6F);
    UnsetTestEnvVar("PROFILER_TEST_FLOAT_VAR");
}

PROFILERTEST(EnvVar, read_float_rejects_garbage_without_throwing)
{
    SetTestEnvVar("PROFILER_TEST_FLOAT_VAR", "not_a_float");
    float value = 1.5F;
    EXPECT_FALSE(profiler::read_float_from_env_var("PROFILER_TEST_FLOAT_VAR", 1.5F, &value));
    EXPECT_NEAR(value, 1.5F, 1e-6F);
    UnsetTestEnvVar("PROFILER_TEST_FLOAT_VAR");
}

PROFILERTEST(EnvVar, read_string_returns_value_or_default)
{
    UnsetTestEnvVar("PROFILER_TEST_STRING_VAR");
    std::string value;
    EXPECT_TRUE(profiler::read_string_from_env_var("PROFILER_TEST_STRING_VAR", "fallback", value));
    EXPECT_EQ(value, "fallback");

    SetTestEnvVar("PROFILER_TEST_STRING_VAR", "explicit_value");
    EXPECT_TRUE(profiler::read_string_from_env_var("PROFILER_TEST_STRING_VAR", "fallback", value));
    EXPECT_EQ(value, "explicit_value");
    UnsetTestEnvVar("PROFILER_TEST_STRING_VAR");
}

PROFILERTEST(EnvVar, read_strings_splits_on_comma_and_trims_whitespace)
{
    SetTestEnvVar("PROFILER_TEST_STRINGS_VAR", "a, b ,c");
    std::vector<std::string> values;
    EXPECT_TRUE(profiler::read_strings_from_env_var("PROFILER_TEST_STRINGS_VAR", "", values));
    ASSERT_EQ(values.size(), 3u);
    EXPECT_EQ(values[0], "a");
    EXPECT_EQ(values[1], "b");
    EXPECT_EQ(values[2], "c");
    UnsetTestEnvVar("PROFILER_TEST_STRINGS_VAR");
}

PROFILERTEST(EnvVar, read_strings_uses_split_default_when_unset)
{
    UnsetTestEnvVar("PROFILER_TEST_STRINGS_VAR");
    std::vector<std::string> values;
    EXPECT_TRUE(profiler::read_strings_from_env_var("PROFILER_TEST_STRINGS_VAR", "x,y", values));
    ASSERT_EQ(values.size(), 2u);
    EXPECT_EQ(values[0], "x");
    EXPECT_EQ(values[1], "y");
}
