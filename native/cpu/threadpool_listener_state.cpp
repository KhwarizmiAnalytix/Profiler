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

#include "native/cpu/threadpool_listener_state.h"

#include <atomic>

namespace profiler::profiler_impl::threadpool_listener
{
namespace
{
std::atomic<int> g_enabled{0};
}  // namespace

bool IsEnabled()
{
    return g_enabled.load(std::memory_order_acquire) != 0;
}

void Activate()
{
    g_enabled.store(1, std::memory_order_release);
}

void Deactivate()
{
    g_enabled.store(0, std::memory_order_release);
}

}  // namespace profiler::profiler_impl::threadpool_listener
