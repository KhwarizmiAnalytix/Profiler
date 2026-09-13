#pragma once

#include <unordered_map>
#include <unordered_set>

namespace profiler
{

template <
    typename Key,
    typename T,
    typename Hash      = std::hash<Key>,
    typename KeyEqual  = std::equal_to<Key>,
    typename Allocator = std::allocator<std::pair<const Key, T>>>
using flat_hash_map = std::unordered_map<Key, T, Hash, KeyEqual, Allocator>;

template <
    typename Key,
    typename Hash      = std::hash<Key>,
    typename KeyEqual  = std::equal_to<Key>,
    typename Allocator = std::allocator<Key>>
using flat_hash_set = std::unordered_set<Key, Hash, KeyEqual, Allocator>;

}  // namespace profiler
