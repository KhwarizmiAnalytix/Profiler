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

#include "bespoke/base/base.h"

#include <cstdint>
#include <functional>

// #include "common/device.h"

namespace profiler::profiler_impl::impl
{

namespace
{
struct DefaultStubs : public ProfilerStubs
{
    explicit DefaultStubs(const char* name) noexcept : name_{name} {}

    void record(
        int16_t* /*device*/, ProfilerVoidEventStub* /*event*/, int64_t* /*cpu_ns*/) const override
    {
        fail();
    }
    float elapsed(const ProfilerVoidEventStub* /*event*/,
        const ProfilerVoidEventStub* /*event2*/) const override
    {
        fail();
        return 0.F;
    }
    void mark(const char* /*name*/) const override { fail(); }
    void rangePush(const char* /*name*/) const override { fail(); }
    void rangePop() const override { fail(); }
    void onEachDevice(std::function<void(int)> /*op*/) const override { fail(); }
    void synchronize() const override { fail(); }
    ~DefaultStubs() override = default;

private:
    void fail() const
    {
        (void)name_;
    }  // PROFILER_CHECK(false, "{} used in profiler but not enabled.", name_);

    // NOLINTNEXTLINE(cppcoreguidelines-avoid-const-or-ref-data-members)
    const char* const name_;
};
}  // namespace

#define REGISTER_DEFAULT(name, upper_name)                                                         \
    namespace                                                                                      \
    {                                                                                              \
    const DefaultStubs            default_##name##_stubs{#upper_name};                             \
    constexpr const DefaultStubs* default_##name##_stubs_addr = &default_##name##_stubs;           \
                                                                                                   \
    /* Constant initialization, so it is guaranteed to be initialized before*/                     \
    /* static initialization calls which may invoke register<name>Methods*/                        \
    inline const ProfilerStubs*& name##_stubs()                                                    \
    {                                                                                              \
        static const ProfilerStubs* stubs_ =                                                       \
            static_cast<const ProfilerStubs*>(default_##name##_stubs_addr);                        \
        return stubs_;                                                                             \
    }                                                                                              \
    } /*namespace*/                                                                                \
                                                                                                   \
    const ProfilerStubs* name##Stubs()                                                             \
    {                                                                                              \
        return name##_stubs();                                                                     \
    }                                                                                              \
                                                                                                   \
    void register##upper_name##Methods(ProfilerStubs* stubs)                                       \
    {                                                                                              \
        name##_stubs() = stubs;                                                                    \
    }

REGISTER_DEFAULT(cuda, CUDA)
REGISTER_DEFAULT(itt, ITT)
REGISTER_DEFAULT(privateuse1, PrivateUse1)
#undef REGISTER_DEFAULT

}  // namespace profiler::profiler_impl::impl
