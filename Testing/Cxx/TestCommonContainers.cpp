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

// Unit tests for the low-level common/ containers and utilities that back the
// rest of Profiler. These are otherwise only exercised indirectly through
// higher-level session/backend integration tests.

#include <atomic>
#include <memory>
#include <string>
#include <thread>
#include <unordered_map>
#include <variant>
#include <vector>

#include "ProfilerTest.h"
#include "common/align_of.h"
#include "common/flat_hash.h"
#include "common/irange.h"
#include "common/lock_free_queue.h"
#include "common/no_init.h"
#include "common/overloaded.h"
#include "common/per_thread.h"
#include "common/small_vector.h"
#include "common/strong_type.h"

// ---------------------------------------------------------------------------
// small_vector
// ---------------------------------------------------------------------------

PROFILERTEST(SmallVector, push_back_grows_from_inline_to_heap)
{
    profiler::small_vector<int, 2> v;
    EXPECT_TRUE(v.empty());
    v.push_back(1);
    v.push_back(2);
    EXPECT_EQ(v.size(), 2u);
    v.push_back(3);  // Exceeds inline capacity; forces heap allocation.
    ASSERT_EQ(v.size(), 3u);
    EXPECT_EQ(v[0], 1);
    EXPECT_EQ(v[1], 2);
    EXPECT_EQ(v[2], 3);
}

PROFILERTEST(SmallVector, pop_back_and_bounds_checked_access)
{
    profiler::small_vector<int, 4> v{1, 2, 3};
    EXPECT_EQ(v.at(2), 3);
    v.pop_back();
    EXPECT_EQ(v.size(), 2u);
    EXPECT_EQ(v.back(), 2);
    EXPECT_EQ(v.front(), 1);
}

PROFILERTEST(SmallVector, erase_and_insert_preserve_order)
{
    profiler::small_vector<int, 4> v{1, 2, 3, 4};
    v.erase(v.begin() + 1);  // Remove '2'.
    ASSERT_EQ(v.size(), 3u);
    EXPECT_EQ(v[0], 1);
    EXPECT_EQ(v[1], 3);
    v.insert(v.begin() + 1, 99);
    ASSERT_EQ(v.size(), 4u);
    EXPECT_EQ(v[1], 99);
    EXPECT_EQ(v[2], 3);
}

PROFILERTEST(SmallVector, move_construction_transfers_heap_buffer)
{
    profiler::small_vector<std::string, 2> v;
    v.push_back("a");
    v.push_back("b");
    v.push_back("c");  // Forces heap allocation.
    profiler::small_vector<std::string, 2> moved(std::move(v));
    ASSERT_EQ(moved.size(), 3u);
    EXPECT_EQ(moved[2], "c");
    EXPECT_TRUE(v.empty());  // NOLINT(bugprone-use-after-move)
}

PROFILERTEST(SmallVector, equality_and_to_vector_helper)
{
    profiler::small_vector<int, 4> a{1, 2, 3};
    profiler::small_vector<int, 4> b{1, 2, 3};
    profiler::small_vector<int, 4> c{1, 2};
    EXPECT_TRUE(a == b);
    EXPECT_TRUE(a != c);

    std::vector<int> src{4, 5, 6};
    auto             converted = profiler::to_vector<4>(src);
    ASSERT_EQ(converted.size(), 3u);
    EXPECT_EQ(converted[0], 4);
    EXPECT_EQ(converted[2], 6);
}

// ---------------------------------------------------------------------------
// flat_hash_map / flat_hash_set
// ---------------------------------------------------------------------------

PROFILERTEST(FlatHash, map_supports_standard_associative_operations)
{
    profiler::flat_hash_map<std::string, int> m;
    m["a"] = 1;
    m["b"] = 2;
    EXPECT_EQ(m.size(), 2u);
    EXPECT_EQ(m.at("a"), 1);
    EXPECT_EQ(m.count("missing"), 0u);
    m.erase("a");
    EXPECT_EQ(m.count("a"), 0u);
}

PROFILERTEST(FlatHash, set_deduplicates_and_erases)
{
    profiler::flat_hash_set<int> s{1, 2, 2, 3};
    EXPECT_EQ(s.size(), 3u);
    EXPECT_NE(s.find(2), s.end());
    s.erase(2);
    EXPECT_EQ(s.count(2), 0u);
}

// ---------------------------------------------------------------------------
// LockFreeQueue / BlockedQueue
// ---------------------------------------------------------------------------

namespace
{
// A tiny block size so pushing a few dozen elements exercises multi-block
// growth and PopAll()'s block-stitching logic.
constexpr size_t kTestQueueBlockSize = 64;
}  // namespace

PROFILERTEST(LockFreeQueue, push_pop_preserves_fifo_order_across_blocks)
{
    profiler::LockFreeQueue<int, kTestQueueBlockSize> queue;
    constexpr int                                     kCount = 50;
    for (int i = 0; i < kCount; ++i)
    {
        int value = i;
        queue.push(std::move(value));
    }
    for (int i = 0; i < kCount; ++i)
    {
        auto popped = queue.pop();
        ASSERT_TRUE(popped.has_value());
        EXPECT_EQ(*popped, i);
    }
    EXPECT_FALSE(queue.pop().has_value());
}

PROFILERTEST(LockFreeQueue, pop_all_preserves_order_and_drains_queue)
{
    profiler::LockFreeQueue<int, kTestQueueBlockSize> queue;
    constexpr int                                     kCount = 30;
    for (int i = 0; i < kCount; ++i)
    {
        int value = i;
        queue.push(std::move(value));
    }

    profiler::BlockedQueue<int, kTestQueueBlockSize> drained  = queue.PopAll();
    int                                              expected = 0;
    for (auto it = drained.begin(); it != drained.end(); ++it)
    {
        EXPECT_EQ(*it, expected++);
    }
    EXPECT_EQ(expected, kCount);
    EXPECT_FALSE(queue.pop().has_value());
}

PROFILERTEST(LockFreeQueue, single_producer_single_consumer_threads_see_all_elements)
{
    profiler::LockFreeQueue<int, kTestQueueBlockSize> queue;
    constexpr int                                     kCount = 500;
    std::atomic<bool>                                 start{false};

    std::thread producer(
        [&]
        {
            while (!start.load(std::memory_order_acquire))
            {
            }
            for (int i = 0; i < kCount; ++i)
            {
                int value = i;
                queue.push(std::move(value));
            }
        });

    std::vector<int> consumed;
    consumed.reserve(kCount);
    std::thread consumer(
        [&]
        {
            start.store(true, std::memory_order_release);
            while (static_cast<int>(consumed.size()) < kCount)
            {
                if (auto v = queue.pop())
                {
                    consumed.push_back(*v);
                }
            }
        });

    producer.join();
    consumer.join();

    ASSERT_EQ(consumed.size(), static_cast<size_t>(kCount));
    for (int i = 0; i < kCount; ++i)
    {
        EXPECT_EQ(consumed[i], i);
    }
}

// Regression for design-review.md Phase 2's "bounded and visible" overflow
// requirement: push() used to grow by allocating a new block forever, with no
// capacity ceiling and no loss accounting anywhere. With max_blocks=2, the
// last slot that would need a third block is dropped instead (and counted),
// every push after that also drops (the check is live, re-evaluated each
// call) -- and capacity recovers once draining frees a block.
PROFILERTEST(LockFreeQueue, push_drops_and_recovers_past_max_blocks)
{
    using Queue = profiler::LockFreeQueue<int, kTestQueueBlockSize>;
    constexpr size_t kMaxBlocks = 2;
    Queue            queue(kMaxBlocks);

    // Filling exactly max_blocks blocks needs one fewer than max_blocks*kNumSlots
    // pushes to succeed: the element that would complete the *last* allowed
    // block is the one push() drops (see lock_free_queue.h's push() comment).
    size_t const capacity = kMaxBlocks * Queue::kNumSlotsPerBlockForTesting - 1;
    for (size_t i = 0; i < capacity; ++i)
    {
        int value = static_cast<int>(i);
        ASSERT_TRUE(queue.push(std::move(value))) << "push " << i << " of " << capacity;
    }
    EXPECT_EQ(queue.dropped_count(), 0u);

    // Further pushes are dropped, and counted, without crashing/corrupting anything.
    int overflow_a = 111;
    int overflow_b = 222;
    EXPECT_FALSE(queue.push(std::move(overflow_a)));
    EXPECT_FALSE(queue.push(std::move(overflow_b)));
    EXPECT_EQ(queue.dropped_count(), 2u);

    // Draining everything frees a block; capacity recovers automatically.
    for (size_t i = 0; i < capacity; ++i)
    {
        auto popped = queue.pop();
        ASSERT_TRUE(popped.has_value());
        EXPECT_EQ(*popped, static_cast<int>(i));
    }
    EXPECT_FALSE(queue.pop().has_value());

    int recovered = 42;
    EXPECT_TRUE(queue.push(std::move(recovered)));
    EXPECT_EQ(queue.dropped_count(), 2u);  // unchanged: this push succeeded
    auto popped = queue.pop();
    ASSERT_TRUE(popped.has_value());
    EXPECT_EQ(*popped, 42);
}

// ---------------------------------------------------------------------------
// per_thread
// ---------------------------------------------------------------------------

PROFILERTEST(PerThread, get_is_stable_within_thread_and_distinct_across_threads)
{
    struct Probe
    {
        int value = 0;
    };

    Probe* main_ptr1 = &profiler::per_thread<Probe>::Get();
    Probe* main_ptr2 = &profiler::per_thread<Probe>::Get();
    EXPECT_EQ(main_ptr1, main_ptr2);

    Probe*      worker_ptr = nullptr;
    std::thread worker([&] { worker_ptr = &profiler::per_thread<Probe>::Get(); });
    worker.join();
    EXPECT_NE(main_ptr1, worker_ptr);
}

PROFILERTEST(PerThread, recording_keeps_instance_alive_after_thread_exit)
{
    struct Probe
    {
        int tag = 0;
    };

    profiler::per_thread<Probe>::StartRecording();
    std::thread worker([] { profiler::per_thread<Probe>::Get().tag = 42; });
    worker.join();

    auto threads = profiler::per_thread<Probe>::StopRecording();
    ASSERT_EQ(threads.size(), 1u);
    EXPECT_EQ(threads[0]->tag, 42);
}

// ---------------------------------------------------------------------------
// irange
// ---------------------------------------------------------------------------

PROFILERTEST(IRange, single_argument_bounds)
{
    std::vector<int> collected;
    for (auto i : profiler::irange(5))
    {
        collected.push_back(i);
    }
    EXPECT_EQ(collected, (std::vector<int>{0, 1, 2, 3, 4}));

    std::vector<int> zero_length;
    for (auto i : profiler::irange(0))
    {
        zero_length.push_back(i);
    }
    EXPECT_TRUE(zero_length.empty());

    std::vector<int> negative;
    for (auto i : profiler::irange(-3))
    {
        negative.push_back(i);
    }
    EXPECT_TRUE(negative.empty());
}

PROFILERTEST(IRange, two_argument_half_open_interval)
{
    std::vector<int> collected;
    for (auto i : profiler::irange(2, 6))
    {
        collected.push_back(i);
    }
    EXPECT_EQ(collected, (std::vector<int>{2, 3, 4, 5}));

    std::vector<int> empty;
    for (auto i : profiler::irange(6, 2))  // end <= begin -> empty range.
    {
        empty.push_back(i);
    }
    EXPECT_TRUE(empty.empty());
}

// ---------------------------------------------------------------------------
// strong_type
// ---------------------------------------------------------------------------

namespace
{
struct DistanceTag
{
};
using Distance = strong::
    type<int, DistanceTag, strong::equality, strong::ordered, strong::arithmetic, strong::hashable>;
}  // namespace

PROFILERTEST(StrongType, equality_and_ordering)
{
    Distance a(3);
    Distance b(3);
    Distance c(5);
    EXPECT_TRUE(a == b);
    EXPECT_TRUE(a != c);
    EXPECT_TRUE(a < c);
    EXPECT_TRUE(c > a);
}

PROFILERTEST(StrongType, arithmetic_operators_forward_to_underlying_value)
{
    Distance a(3);
    Distance b(4);
    Distance sum = a + b;
    EXPECT_EQ(value_of(sum), 7);
    sum += Distance(1);
    EXPECT_EQ(value_of(sum), 8);
}

PROFILERTEST(StrongType, hashable_modifier_enables_use_as_map_key)
{
    std::unordered_map<Distance, std::string> m;
    m[Distance(1)] = "one";
    m[Distance(2)] = "two";
    EXPECT_EQ(m.at(Distance(1)), "one");
    EXPECT_EQ(m.size(), 2u);
}

// ---------------------------------------------------------------------------
// align_of
// ---------------------------------------------------------------------------

PROFILERTEST(AlignOf, union_has_max_alignment_and_sufficient_size)
{
    using Buffer = profiler::AlignedCharArrayUnion<char, double, int>;
    EXPECT_GE(alignof(Buffer), alignof(double));
    EXPECT_GE(sizeof(Buffer), sizeof(double));

    Buffer  buffer;
    double* as_double = ::new (static_cast<void*>(&buffer)) double(3.5);
    EXPECT_NEAR(*as_double, 3.5, 1e-9);
}

// ---------------------------------------------------------------------------
// overloaded
// ---------------------------------------------------------------------------

PROFILERTEST(Overloaded, dispatches_to_the_matching_lambda)
{
    std::variant<int, std::string> value = 42;
    auto                           visitor =
        profiler::overloaded([](int v) { return std::string("int:") + std::to_string(v); },
            [](const std::string& v) { return std::string("string:") + v; });
    EXPECT_EQ(std::visit(visitor, value), "int:42");

    value = std::string("hello");
    EXPECT_EQ(std::visit(visitor, value), "string:hello");
}

// ---------------------------------------------------------------------------
// no_init
// ---------------------------------------------------------------------------

namespace
{
struct LifecycleProbe
{
    static int constructed;
    static int destructed;

    explicit LifecycleProbe(int v) : value(v) { ++constructed; }
    LifecycleProbe(LifecycleProbe&& other) noexcept : value(other.value) { ++constructed; }
    LifecycleProbe(const LifecycleProbe&)            = delete;
    LifecycleProbe& operator=(const LifecycleProbe&) = delete;
    LifecycleProbe& operator=(LifecycleProbe&&)      = delete;
    ~LifecycleProbe() { ++destructed; }

    int value;
};
int LifecycleProbe::constructed = 0;
int LifecycleProbe::destructed  = 0;
}  // namespace

PROFILERTEST(NoInit, default_construction_does_not_run_wrapped_constructor)
{
    LifecycleProbe::constructed = 0;
    LifecycleProbe::destructed  = 0;
    {
        profiler::no_init<LifecycleProbe> slot;
        EXPECT_EQ(LifecycleProbe::constructed, 0);
    }
    // no_init's destructor is intentionally a no-op.
    EXPECT_EQ(LifecycleProbe::destructed, 0);
}

PROFILERTEST(NoInit, emplace_and_consume_run_wrapped_lifecycle)
{
    LifecycleProbe::constructed = 0;
    LifecycleProbe::destructed  = 0;
    profiler::no_init<LifecycleProbe> slot;
    slot.emplace(7);
    EXPECT_EQ(LifecycleProbe::constructed, 1);

    LifecycleProbe consumed = std::move(slot).consume();
    EXPECT_EQ(consumed.value, 7);
    EXPECT_EQ(LifecycleProbe::destructed, 1);  // The slot's original was destroyed by consume().
}

PROFILERTEST(NoInit, destroy_invokes_wrapped_destructor)
{
    LifecycleProbe::constructed = 0;
    LifecycleProbe::destructed  = 0;
    profiler::no_init<LifecycleProbe> slot;
    slot.Emplace(9);
    slot.Destroy();
    EXPECT_EQ(LifecycleProbe::destructed, 1);
}
