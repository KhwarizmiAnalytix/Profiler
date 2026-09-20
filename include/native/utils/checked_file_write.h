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

#include <string>

#include "common/profiler_export.h"

namespace profiler
{
namespace profiler_impl
{

/**
 * @brief Writes `content` to `path`, publishing it only once fully written.
 *
 * Writes to a sibling temporary path first, checks the write and close both
 * succeeded, then publishes atomically (rename) over `path`. On any failure,
 * removes the temporary file and leaves an existing valid `path` untouched --
 * a caller never observes a partially-written destination file.
 *
 * @return true if `path` now contains `content` in full.
 */
PROFILER_API bool write_file_checked(const std::string& path, const std::string& content);

/**
 * @brief Publishes `path + ".tmp"` the same way write_file_checked() publishes
 * in-memory content, for writers with no success/failure return value of their
 * own (e.g. libkineto's void-returning ActivityTraceInterface::save(), which
 * must be pointed at the temp path by the caller before this is called).
 *
 * Verifies the temp file exists, then publishes it atomically (rename) over
 * `path`. On any failure, removes the temp file and leaves an existing valid
 * `path` untouched. This cannot detect a partial write the way
 * write_file_checked() can (there is no in-memory content to have written in
 * full) -- it is the best available signal when the underlying writer gives
 * none of its own.
 *
 * @return true if `path` was published from the temp file.
 */
PROFILER_API bool publish_external_temp_file(const std::string& path);

}  // namespace profiler_impl
}  // namespace profiler
