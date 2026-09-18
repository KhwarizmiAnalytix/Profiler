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
#include "native/core/profiler_collection.h"

#include <exception>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "native/core/profiler_interface.h"
#include "native/exporters/xplane/xplane.h"

namespace profiler
{

namespace
{

// design-review.md section 6.7: "Keep normal scope recording and callbacks
// nonthrowing with contained failure handling" -- profiler_interface
// implementations are required-component collectors (host/GPU/python
// tracers, MetadataCollector, ...), not first-party-controlled code all the
// way down (a vendor callback could misbehave), and profiler_collection is
// the one place every one of them funnels through on the way to the public
// session API. Without this, a throwing collector propagates a C++
// exception straight across session::start()/stop()/collect_data() to
// whatever application called it -- reproduced directly: an
// std::runtime_error thrown from a collector's start() escaped
// profiler_collection::start() uncaught before this fix, which would
// std::terminate() a caller that (reasonably) didn't wrap the public API in
// its own try/catch. Catching here converts it into an ordinary
// profiler_status::Error(), the same contract every other
// profiler_interface failure already uses -- callers can't tell "backend
// returned an error status" from "backend threw," which is deliberate: the
// difference is an implementation detail already isolated at the
// collection boundary, not one downstream API surface should have to know
// about.
template <typename Fn> profiler_status call_one(const char* stage_name, Fn&& fn)
{
    try
    {
        return fn();
    }
    catch (const std::exception& e)
    {
        return profiler_status::Error(
            std::string("profiler backend threw during ") + stage_name + ": " + e.what());
    }
    catch (...)
    {
        return profiler_status::Error(
            std::string("profiler backend threw a non-std::exception during ") + stage_name);
    }
}

}  // namespace

profiler_collection::profiler_collection(std::vector<std::unique_ptr<profiler_interface>> profilers)
    : profilers_(std::move(profilers))
{
}

profiler_status profiler_collection::start()
{
    bool        ok = true;
    std::string errors;
    for (auto& profiler : profilers_)
    {
        profiler_status const status = call_one("start()", [&] { return profiler->start(); });
        if (!status.ok())
        {
            ok = false;
            if (!status.message().empty())
            {
                if (!errors.empty())
                {
                    errors.append("\n");
                }
                errors.append(status.message());
            }
        }
    }
    if (ok)
    {
        return profiler_status::Ok();
    }
    return errors.empty() ? profiler_status::Error("Failed to start profiler backends.")
                          : profiler_status::Error(std::move(errors));
}

profiler_status profiler_collection::stop()
{
    bool        ok = true;
    std::string errors;
    for (auto& profiler : profilers_)
    {
        profiler_status const status = call_one("stop()", [&] { return profiler->stop(); });
        if (!status.ok())
        {
            ok = false;
            if (!status.message().empty())
            {
                if (!errors.empty())
                {
                    errors.append("\n");
                }
                errors.append(status.message());
            }
        }
    }
    if (ok)
    {
        return profiler_status::Ok();
    }
    return errors.empty() ? profiler_status::Error("Failed to stop profiler backends.")
                          : profiler_status::Error(std::move(errors));
}

profiler_status profiler_collection::collect_data(x_space* space)
{
    bool        ok = true;
    std::string errors;

    for (auto& profiler : profilers_)
    {
        profiler_status const status =
            call_one("collect_data()", [&] { return profiler->collect_data(space); });
        if (!status.ok())
        {
            ok = false;
            if (!status.message().empty())
            {
                if (!errors.empty())
                {
                    errors.append("\n");
                }
                errors.append(status.message());
            }
        }
    }
    profilers_.clear();  // data has been collected
    if (ok)
    {
        return profiler_status::Ok();
    }
    return errors.empty() ? profiler_status::Error("Failed to collect profiler backend data.")
                          : profiler_status::Error(std::move(errors));
}
}  // namespace profiler
