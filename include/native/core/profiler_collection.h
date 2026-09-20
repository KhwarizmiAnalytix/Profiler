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

/* Copyright 2022 The TensorFlow Authors. All Rights Reserved.

Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at

    http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License.
==============================================================================*/
#pragma once

#include <memory>  // for unique_ptr
#include <vector>  // for vector

#include "common/profiler_export.h"          // for PROFILER_API, PROFILER_VISIBILITY
#include "native/core/profiler_interface.h"  // for profiler_interface
namespace profiler
{
class x_space;
}

namespace profiler
{

// profiler_collection multiplexes profiler_interface calls into a collection of
// profilers. Exported (unlike a purely internal implementation detail) so
// first-party failure-injection tests can construct one directly with a
// test-only profiler_interface -- see
// Testing/Cxx/TestProfilerFailureInjection.cpp.
class PROFILER_VISIBILITY profiler_collection : public profiler_interface
{
public:
    PROFILER_API explicit profiler_collection(
        std::vector<std::unique_ptr<profiler_interface>> profilers);

    PROFILER_API profiler_status start() override;

    PROFILER_API profiler_status stop() override;

    PROFILER_API profiler_status collect_data(x_space* space) override;

private:
    std::vector<std::unique_ptr<profiler_interface>> profilers_;
};

}  // namespace profiler
