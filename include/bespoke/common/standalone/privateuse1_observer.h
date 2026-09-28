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
#include "bespoke/common/api.h"

namespace profiler::profiler_impl::impl
{

using CallBackFnPtr = void (*)(
    const ProfilerConfig& config, const std::unordered_set<profiler::RecordScope>& scopes);

struct PushPRIVATEUSE1CallbacksStub
{
    PushPRIVATEUSE1CallbacksStub()                                               = default;
    PushPRIVATEUSE1CallbacksStub(const PushPRIVATEUSE1CallbacksStub&)            = delete;
    PushPRIVATEUSE1CallbacksStub& operator=(const PushPRIVATEUSE1CallbacksStub&) = delete;
    PushPRIVATEUSE1CallbacksStub(PushPRIVATEUSE1CallbacksStub&&)                 = default;
    PushPRIVATEUSE1CallbacksStub& operator=(PushPRIVATEUSE1CallbacksStub&&)      = default;
    ~PushPRIVATEUSE1CallbacksStub()                                              = default;

    explicit operator bool() const noexcept { return push_privateuse1_callbacks_fn != nullptr; }

    template <typename... ArgTypes> void operator()(ArgTypes&&... args)
    {
        if (push_privateuse1_callbacks_fn == nullptr)
        {
            return;
        }
        return (*push_privateuse1_callbacks_fn)(std::forward<ArgTypes>(args)...);
    }

    void set_privateuse1_dispatch_ptr(CallBackFnPtr fn_ptr)
    {
        push_privateuse1_callbacks_fn = fn_ptr;
    }

private:
    CallBackFnPtr push_privateuse1_callbacks_fn = nullptr;
};

extern PROFILER_API struct PushPRIVATEUSE1CallbacksStub pushPRIVATEUSE1CallbacksStub;

struct RegisterPRIVATEUSE1Observer
{
    RegisterPRIVATEUSE1Observer(PushPRIVATEUSE1CallbacksStub& stub, CallBackFnPtr value)
    {
        stub.set_privateuse1_dispatch_ptr(value);
    }
};

#define REGISTER_PRIVATEUSE1_OBSERVER(name, fn)                                                    \
    static RegisterPRIVATEUSE1Observer name##__register(name, fn);
}  // namespace profiler::profiler_impl::impl
