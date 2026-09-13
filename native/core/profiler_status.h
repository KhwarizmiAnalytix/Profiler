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

#pragma once

#include <string>
#include <utility>

namespace profiler
{

class profiler_status
{
public:
    static profiler_status Ok() { return profiler_status(true, {}); }
    static profiler_status Error(std::string message)
    {
        return profiler_status(false, std::move(message));
    }

    bool               ok() const { return ok_; }
    const std::string& message() const { return message_; }

    explicit operator bool() const { return ok_; }

private:
    profiler_status(bool ok, std::string message) : ok_(ok), message_(std::move(message)) {}

    bool        ok_;
    std::string message_;
};

}  // namespace profiler
