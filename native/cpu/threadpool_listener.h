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

#pragma once

#include <cstdint>
#include <memory>
#include <string>

#include "common/profiler_export.h"
#include "native/core/profiler_interface.h"
#include "native/exporters/xplane/xplane.h"
#include "native/tracing/tracing.h"

namespace profiler::profiler_impl
{

class threadpool_event_collector : public profiler::tracing::event_collector
{
public:
    threadpool_event_collector() = default;

    void record_event(uint64_t arg) const override;
    void start_region(uint64_t arg) const override;
    void stop_region() const override;
};

class threadpool_profiler_interface : public profiler_interface
{
public:
    threadpool_profiler_interface() = default;

    profiler_status start() override;
    profiler_status stop() override;
    profiler_status collect_data(x_space* space) override;

private:
    profiler_status last_status_ = profiler_status::Ok();
};

std::unique_ptr<profiler_interface> create_threadpool_profiler();

}  // namespace profiler::profiler_impl
