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

#if defined(__linux__) && (defined(__x86_64__) || defined(__aarch64__)) && defined(FBCODE_CAFFE2)

#include <llvm/DebugInfo/Symbolize/Symbolize.h>
#include <profiler/util/flat_hash_map.h>

#include "bespoke/common/unwind/unwind.h"

namespace profiler::unwind
{

std::vector<Frame> symbolize(const std::vector<void*>& frames, Mode mode)
{
    static std::mutex                            symbolize_mutex;
    static llvm::symbolize::LLVMSymbolizer       symbolizer;
    static profiler::flat_hash_map<void*, Frame> frame_map_;

    std::lock_guard<std::mutex> guard(symbolize_mutex);
    std::vector<Frame>          results;
    results.reserve(frames.size());
    for (auto addr : frames)
    {
        if (!frame_map_.count(addr))
        {
            auto frame         = Frame{"??", "<unwind unsupported>", 0};
            auto maybe_library = libraryFor(addr);
            if (maybe_library)
            {
                auto libaddress = maybe_library->second - 1;
                auto r          = symbolizer.symbolizeCode(maybe_library->first,
                             {libaddress, llvm::object::SectionedAddress::UndefSection});
                if (r)
                {
                    frame.filename = r->FileName;
                    frame.funcname = r->FunctionName;
                    frame.lineno   = r->Line;
                }
            }
            frame_map_[addr] = std::move(frame);
        }
        results.emplace_back(frame_map_[addr]);
    }
    return results;
}

}  // namespace profiler::unwind

#endif
