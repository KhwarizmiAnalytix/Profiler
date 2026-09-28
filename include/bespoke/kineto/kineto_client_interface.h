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

// TODO: Missing Profiler dependency - original include was:
// #include <profiler/csrc/jit/runtime/interpreter.h>
// This is a Profiler-specific header not available in Profiler

#include "common/profiler_export.h"

namespace profiler
{

// declare global_kineto_init for libtorch_cpu.so to call
PROFILER_API void global_kineto_init();

}  // namespace profiler
