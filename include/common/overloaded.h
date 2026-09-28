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

#include <memory>
namespace profiler
{
namespace detail
{

template <class... Ts> struct overloaded_t
{
};

template <class T0> struct overloaded_t<T0> : T0
{
    using T0::operator();
    overloaded_t(T0 t0) : T0(std::move(t0)) {}
};
template <class T0, class... Ts> struct overloaded_t<T0, Ts...> : T0, overloaded_t<Ts...>
{
    using T0::operator();
    using overloaded_t<Ts...>::operator();
    overloaded_t(T0 t0, Ts... ts) : T0(std::move(t0)), overloaded_t<Ts...>(std::move(ts)...) {}
};

}  // namespace detail

// Construct an overloaded callable combining multiple callables, e.g. lambdas
template <class... Ts> detail::overloaded_t<Ts...> overloaded(Ts... ts)
{
    return {std::move(ts)...};
}

}  // namespace profiler
