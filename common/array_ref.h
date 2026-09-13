//===--- array_ref.h - Array Reference Wrapper -------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

// Profiler: modified from llvm::array_ref.
// removed llvm-specific functionality
// removed some implicit const -> non-const conversions that rely on
// complicated std::enable_if meta-programming
// removed a bunch of slice variants for simplicity...

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <iterator>
#include <ostream>
#include <type_traits>
#include <vector>

#include "common/profiler_macros.h"
//#include "util/exception.h"
#include "common/small_vector.h"

namespace profiler
{
/// array_ref - Represent a constant reference to an array (0 or more elements
/// consecutively in memory), i.e. a start pointer and a length.  It allows
/// various APIs to take consecutive elements easily and conveniently.
///
/// This class does not own the underlying data, it is expected to be used in
/// situations where the data resides in some other buffer, whose lifetime
/// extends past that of the array_ref. For this reason, it is not in general
/// safe to store an array_ref.
///
/// This is intended to be trivially copyable, so it should be passed by
/// value.
template <typename T>
class array_ref final
{
public:
    using iterator       = const T*;
    using const_iterator = const T*;
    using size_type      = size_t;
    using value_type     = T;

    using reverse_iterator = std::reverse_iterator<iterator>;

private:
    /// The start of the array, in an external buffer.
    const T* Data;

    /// The number of elements.
    size_type Length;

    void debugCheckNullptrInvariant()
    {
        // PROFILER_CHECK_DEBUG(
        // Data != nullptr || Length == 0,
        // "created array_ref with nullptr and non-zero length! std::optional relies on this "
        // "being illegal");
    }

public:
    /// @name Constructors
    /// @{

    /// Construct an empty array_ref.
    /* implicit */ constexpr array_ref() : Data(nullptr), Length(0) {}

    /// Construct an array_ref from a single element.
    // TODO Make this explicit
    constexpr array_ref(const T& OneElt) : Data(&OneElt), Length(1) {}

    /// Construct an array_ref from a pointer and length.
    constexpr array_ref(const T* data, size_t length) : Data(data), Length(length)
    {
        debugCheckNullptrInvariant();
    }

    /// Construct an array_ref from a range.
    constexpr array_ref(const T* begin, const T* end) : Data(begin), Length(end - begin)
    {
        debugCheckNullptrInvariant();
    }

    /// Construct an array_ref from a small_vector. This is templated in order to
    /// avoid instantiating SmallVectorTemplateCommon<T> whenever we
    /// copy-construct an array_ref.
    template <typename U>
    /* implicit */ array_ref(const SmallVectorTemplateCommon<T, U>& Vec)
        : Data(Vec.data()), Length(Vec.size())
    {
        debugCheckNullptrInvariant();
    }

    template <
        typename Container,
        typename U = decltype(std::declval<Container>().data()),
        typename   = std::enable_if_t<(std::is_same_v<U, T*> || std::is_same_v<U, T const*>)>>
    /* implicit */ array_ref(const Container& container)
        : Data(container.data()), Length(container.size())
    {
        debugCheckNullptrInvariant();
    }

    /// Construct an array_ref from a std::vector.
    // The enable_if stuff here makes sure that this isn't used for
    // std::vector<bool>, because array_ref can't work on a std::vector<bool>
    // bitfield.
    template <typename A>
    /* implicit */ array_ref(const std::vector<T, A>& Vec) : Data(Vec.data()), Length(Vec.size())
    {
        static_assert(
            !std::is_same_v<T, bool>,
            "array_ref<bool> cannot be constructed from a std::vector<bool> bitfield.");
    }

    /// Construct an array_ref from a std::array
    template <size_t N>
    /* implicit */ constexpr array_ref(const std::array<T, N>& Arr) : Data(Arr.data()), Length(N)
    {
    }

    /// Construct an array_ref from a C array.
    template <size_t N>
    // NOLINTNEXTLINE(*c-arrays*)
    /* implicit */ constexpr array_ref(const T (&Arr)[N]) : Data(Arr), Length(N)
    {
    }

    /// Construct an array_ref from a std::initializer_list.
    /* implicit */ constexpr array_ref(const std::initializer_list<T>& Vec)
        : Data(std::begin(Vec) == std::end(Vec) ? static_cast<T*>(nullptr) : std::begin(Vec)),
          Length(Vec.size())
    {
    }

    /// @}
    /// @name Simple Operations
    /// @{

    constexpr iterator begin() const { return Data; }
    constexpr iterator end() const { return Data + Length; }

    // These are actually the same as iterator, since array_ref only
    // gives you const iterators.
    constexpr const_iterator cbegin() const { return Data; }
    constexpr const_iterator cend() const { return Data + Length; }

    constexpr reverse_iterator rbegin() const { return reverse_iterator(end()); }
    constexpr reverse_iterator rend() const { return reverse_iterator(begin()); }

    /// Check if all elements in the array satisfy the given expression
    constexpr bool allMatch(const std::function<bool(const T&)>& pred) const
    {
        return std::all_of(cbegin(), cend(), pred);
    }

    /// empty - Check if the array is empty.
    constexpr bool empty() const { return Length == 0; }

    constexpr const T* data() const { return Data; }

    /// size - Get the array size.
    constexpr size_t size() const { return Length; }

    /// front - Get the first element.
    constexpr const T& front() const
    {
        //PROFILER_CHECK(!empty(), "array_ref: attempted to access front() of empty list");
        return Data[0];
    }

    /// back - Get the last element.
    constexpr const T& back() const
    {
        //PROFILER_CHECK(!empty(), "array_ref: attempted to access back() of empty list");
        return Data[Length - 1];
    }

    /// equals - Check for element-wise equality.
    constexpr bool equals(array_ref RHS) const
    {
        return Length == RHS.Length && std::equal(begin(), end(), RHS.begin());
    }

    /// slice(n, m) - Take M elements of the array starting at element N
    constexpr array_ref<T> slice(size_t N, size_t M) const
    {
        //PROFILER_CHECK(
        //   N + M <= size(), "array_ref: invalid slice, N = ", N, "; M = ", M, "; size = ", size());
        return array_ref<T>(data() + N, M);
    }

    /// slice(n) - Chop off the first N elements of the array.
    constexpr array_ref<T> slice(size_t N) const
    {
        //PROFILER_CHECK(N <= size(), "array_ref: invalid slice, N = ", N, "; size = ", size());
        return slice(N, size() - N);
    }

    /// @}
    /// @name Operator Overloads
    /// @{
    constexpr const T& operator[](size_t Index) const { return Data[Index]; }

    /// Vector compatibility
    constexpr const T& at(size_t Index) const
    {
        //PROFILER_CHECK(
        //   Index < Length, "array_ref: invalid index Index = ", Index, "; Length = ", Length);
        return Data[Index];
    }

    /// Disallow accidental assignment from a temporary.
    ///
    /// The declaration here is extra complicated so that "arrayRef = {}"
    /// continues to select the move assignment operator.
    template <typename U>
    std::enable_if_t<std::is_same_v<U, T>, array_ref<T>>& operator=(
        // NOLINTNEXTLINE(cppcoreguidelines-missing-std-forward)
        U&& Temporary) = delete;

    /// Disallow accidental assignment from a temporary.
    ///
    /// The declaration here is extra complicated so that "arrayRef = {}"
    /// continues to select the move assignment operator.
    template <typename U>
    std::enable_if_t<std::is_same_v<U, T>, array_ref<T>>& operator=(std::initializer_list<U>) =
        delete;

    /// @}
    /// @name Expensive Operations
    /// @{
    std::vector<T> vec() const { return std::vector<T>(Data, Data + Length); }

    /// @}
};

template <typename T>
std::ostream& operator<<(std::ostream& out, array_ref<T> list)
{
    int i = 0;
    out << "[";
    for (const auto& e : list)
    {
        if (i++ > 0)
            out << ", ";
        out << e;
    }
    out << "]";
    return out;
}

/// @name array_ref Convenience constructors
/// @{

/// Construct an array_ref from a single element.
template <typename T>
array_ref<T> makeArrayRef(const T& OneElt)
{
    return OneElt;
}

/// Construct an array_ref from a pointer and length.
template <typename T>
array_ref<T> makeArrayRef(const T* data, size_t length)
{
    return array_ref<T>(data, length);
}

/// Construct an array_ref from a range.
template <typename T>
array_ref<T> makeArrayRef(const T* begin, const T* end)
{
    return array_ref<T>(begin, end);
}

/// Construct an array_ref from a small_vector.
template <typename T>
array_ref<T> makeArrayRef(const SmallVectorImpl<T>& Vec)
{
    return Vec;
}

/// Construct an array_ref from a small_vector.
template <typename T, unsigned N>
array_ref<T> makeArrayRef(const small_vector<T, N>& Vec)
{
    return Vec;
}

/// Construct an array_ref from a std::vector.
template <typename T>
array_ref<T> makeArrayRef(const std::vector<T>& Vec)
{
    return Vec;
}

/// Construct an array_ref from a std::array.
template <typename T, std::size_t N>
array_ref<T> makeArrayRef(const std::array<T, N>& Arr)
{
    return Arr;
}

/// Construct an array_ref from an array_ref (no-op) (const)
template <typename T>
array_ref<T> makeArrayRef(const array_ref<T>& Vec)
{
    return Vec;
}

/// Construct an array_ref from an array_ref (no-op)
template <typename T>
array_ref<T>& makeArrayRef(array_ref<T>& Vec)
{
    return Vec;
}

/// Construct an array_ref from a C array.
template <typename T, size_t N>
// NOLINTNEXTLINE(*c-arrays*)
array_ref<T> makeArrayRef(const T (&Arr)[N])
{
    return array_ref<T>(Arr);
}

// WARNING: Template instantiation will NOT be willing to do an implicit
// conversions to get you to an profiler::array_ref, which is why we need so
// many overloads.

template <typename T>
bool operator==(profiler::array_ref<T> a1, profiler::array_ref<T> a2)
{
    return a1.equals(a2);
}

template <typename T>
bool operator!=(profiler::array_ref<T> a1, profiler::array_ref<T> a2)
{
    return !a1.equals(a2);
}

template <typename T>
bool operator==(const std::vector<T>& a1, profiler::array_ref<T> a2)
{
    return profiler::array_ref<T>(a1).equals(a2);
}

template <typename T>
bool operator!=(const std::vector<T>& a1, profiler::array_ref<T> a2)
{
    return !profiler::array_ref<T>(a1).equals(a2);
}

template <typename T>
bool operator==(profiler::array_ref<T> a1, const std::vector<T>& a2)
{
    return a1.equals(profiler::array_ref<T>(a2));
}

template <typename T>
bool operator!=(profiler::array_ref<T> a1, const std::vector<T>& a2)
{
    return !a1.equals(profiler::array_ref<T>(a2));
}

using IntArrayRef = array_ref<int64_t>;

using IntList [[deprecated(
    "This alias is deprecated because it doesn't make ownership semantics obvious. Use IntArrayRef "
    "instead!")]] = array_ref<int64_t>;

}  // namespace profiler
