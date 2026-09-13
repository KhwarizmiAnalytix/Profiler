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

/* Copyright 2024 The TensorFlow Authors. All Rights Reserved.

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

#include <memory>
#include <vector>

#include "native/exporters/xplane/xplane_builder.h"

namespace profiler
{

/*
 * Subclass of this interface will perform different mutatation to the event.
 * Checking eligibilities of event mutation is not responsible of this class.
 */
class XplaneEventMutator
{
public:
    virtual ~XplaneEventMutator() = default;

    // Mutate event by event specified by the event_metadata.
    virtual void Mutate(XEventBuilder& builder) = 0;
    // Mutate line by line if event_metadata() return nullptr.
    virtual void MutateEventsInLine(XLineBuilder& line) = 0;

    const XEventMetadata* event_metadata() const { return event_metadata_; }

protected:
    explicit XplaneEventMutator(XEventMetadata* event_metadata) : event_metadata_(event_metadata) {}

    XEventMetadata* event_metadata_;
};

class XplaneEventMutatorFactory
{
public:
    virtual ~XplaneEventMutatorFactory() = default;

    virtual std::vector<std::unique_ptr<XplaneEventMutator>> CreateMutators(
        XPlaneBuilder& xplane) const = 0;

protected:
    XplaneEventMutatorFactory() = default;
};

}  // namespace profiler
