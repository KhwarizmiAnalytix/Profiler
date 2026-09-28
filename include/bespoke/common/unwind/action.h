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
#include <ostream>

namespace profiler::unwind
{

enum
{
    A_UNDEFINED           = 0x0,
    A_REG_PLUS_DATA       = 0x1,  // exp = REG[reg] + data0
    A_LOAD_CFA_OFFSET     = 0x2,  // exp = *(cfa + data0)
    A_REG_PLUS_DATA_DEREF = 0x3   // exp = *(REG[reg] + data0)
};

// register numbers in dwarf info
enum
{
    D_UNDEFINED = -1,
    D_RBP       = 6,
    D_RSP       = 7,
    D_RIP       = 16,
    D_REG_SIZE  = 17,
};

struct Action
{
    uint8_t       kind = A_UNDEFINED;
    int32_t       reg  = -1;
    int64_t       data = 0;
    static Action undefined() { return Action{A_UNDEFINED}; }
    static Action regPlusData(int32_t reg, int64_t offset)
    {
        return Action{A_REG_PLUS_DATA, reg, offset};
    }
    static Action regPlusDataDeref(int32_t reg, int64_t offset)
    {
        return Action{A_REG_PLUS_DATA_DEREF, reg, offset};
    }
    static Action loadCfaOffset(int64_t offset)
    {
        return Action{A_LOAD_CFA_OFFSET, D_UNDEFINED, offset};
    }

    friend std::ostream& operator<<(std::ostream& out, const Action& self)
    {
        switch (self.kind)
        {
        case A_UNDEFINED:
            out << "u";
            break;
        case A_REG_PLUS_DATA:
            out << "r" << (int)self.reg << " + " << self.data;
            break;
        case A_REG_PLUS_DATA_DEREF:
            out << "*(r" << (int)self.reg << " + " << self.data << ")";
            break;
        case A_LOAD_CFA_OFFSET:
            out << "*(cfa + " << self.data << ")";
            break;
        }
        return out;
    }
};

}  // namespace profiler::unwind
