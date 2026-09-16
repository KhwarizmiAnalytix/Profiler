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

#include <stddef.h>

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <optional>
#include <utility>

#include "common/profiler_macros.h"
////#include "logger/logger.h"
//#include "util/exception.h"
#include "common/no_init.h"

namespace profiler
{
namespace QueueBaseInternal
{

// Internally implement the base block listed queue which will later extends to:
//    * A single-producer single-consumer queue -- LockFreeQueue
//    * A normal BlockedQueue which is not concerning the concurrency
//
// The queue base is a linked-list of blocks containing numbered slots, with
// start and end pointers:
//
//  [ slots........ | next-]--> [ slots......... | next ]
//  ^start_block_ ^start_         ^end_block_ ^end_
//
// start_ is the first occupied slot, end_ is the first unoccupied slot.
//
// Push writes at end_, and then advances it, allocating a block if needed.
// Pop takes ownership of the element at start_, if any.
// Clear removes all elements in the range [start_, end_).
//
// end_ will be atomic<size_t> when using under single-producer single-consumer
// case, or it could be size_t for a thread-compatible data structure. In the
// single-producer single-consumer scenario,
//   Push and Pop are lock free and each might be called from a single thread.
// Push is called by the producer thread. Pop is called by the consumer thread.
// Since Pop might race with Push, Pop only removes an element if Push finished
// before Pop was called. If Push is called while Pop is active, the new element
// remains in the queue.
//
// Beyond the QueueBase,
//   * LockFreeQueue's PopAll() will generate a BlockedQueue efficiently
//   * BlockedQueue support move constructor/assignment and iterators

// Swaps two std::atomic<T> values (std::atomic itself has no swap()). Only used
// for single-threaded bookkeeping (move-assignment of a not-yet-shared queue),
// so relaxed loads/stores are fine.
template <typename T>
void swap_atomic(std::atomic<T>& a, std::atomic<T>& b)
{
    T const a_value = a.load(std::memory_order_relaxed);
    a.store(b.load(std::memory_order_relaxed), std::memory_order_relaxed);
    b.store(a_value, std::memory_order_relaxed);
}

template <typename T, size_t kBlockSize>
struct InternalBlock
{
    // The number of slots in a block is chosen so the block fits in kBlockSize.
    static constexpr size_t kNumSlots =
        (kBlockSize - (sizeof(size_t /*start*/) + sizeof(InternalBlock* /*next*/))) /
        sizeof(no_init<T>);

    size_t         start;  // The number of the first slot.
    InternalBlock* next;
    no_init<T>     slots[kNumSlots];
};

// Wraps size_t or std::atomic<size_t> used as index to the queue.
// Index<true> wraps std::atomic<size_t>, Index<false> wraps size_t.
template <bool kIsAtomic>
struct Index;

template <>
struct Index<false>
{
    size_t value;
    explicit Index(size_t pos = 0) : value(pos) {}
    size_t get() const { return value; }
    void   set(size_t pos) { value = pos; }
};

template <>
struct Index<true>
{
    std::atomic<size_t> value;
    explicit Index(size_t pos = 0) : value(pos) {}
    size_t get() const { return value.load(std::memory_order_acquire); }
    void   set(size_t pos) { value.store(pos, std::memory_order_release); }
};

template <typename T, size_t kBlockSize, bool kAtomicEnd>
class blocked_queue_base
{
    using Block = InternalBlock<T, kBlockSize>;

public:
    static constexpr size_t kNumSlotsPerBlockForTesting = Block::kNumSlots;

    // Default: kDefaultMaxBlocks blocks of kBlockSize bytes each before push()
    // starts dropping -- 1024 * 64 KiB = 64 MiB per queue at the library-wide
    // default kBlockSize, bounding a single long-running, undrained producer
    // instead of growing forever (see push()/dropped_count()).
    static constexpr size_t kDefaultMaxBlocks = 1024;

    explicit blocked_queue_base(size_t max_blocks = kDefaultMaxBlocks)
        : max_blocks_(max_blocks),
          start_block_(new Block{/*start=*/0, /*next=*/nullptr, /*slots=*/{}}),
          start_(start_block_->start),
          end_block_(start_block_),
          end_(end_block_->start)
    {
    }

    // Memory should be deallocated and elements destroyed on destruction.
    // This doesn't require global lock as this discards all the stored elements
    // and we assume of destruction of this instance only after the last push()
    // has been called.
    ~blocked_queue_base()
    {
        clear();
        if (!empty())
        {
            // PROFILER_LOG_WARNING("Queue is not empty in destructor");
        }
        delete end_block_;
    }

    // Adds a new element to the back of the queue. Fast and lock-free on the
    // producer thread. Returns false (and counts the drop -- see
    // dropped_count()) instead of growing past max_blocks_ live blocks; capacity
    // frees up automatically as the consumer thread drains blocks (pop_impl()),
    // so this is a live check, not a one-shot/permanent cutoff.
    //
    // Capacity is checked *before* a write that would fill the current block,
    // and only that write is dropped -- end_block_ never reaches "full with no
    // successor block" (pop_impl()'s existing delete-on-drain logic relies on a
    // full block always having one). The element that would have filled it is
    // re-attempted on every subsequent push() (the check re-reads block_count_
    // live each time), so a block is completed and rotated normally as soon as
    // the consumer frees enough capacity.
    bool push(T&& element)
    {
        size_t const end            = get_end();
        bool const   would_fill_block = (end - end_block_->start + 1 == Block::kNumSlots);
        if PROFILER_UNLIKELY (would_fill_block &&
                               block_count_.load(std::memory_order_relaxed) >= max_blocks_)
        {
            dropped_count_.fetch_add(1, std::memory_order_relaxed);
            return false;
        }
        auto& slot = end_block_->slots[end - end_block_->start];
        slot.emplace(std::move(element));
        size_t const new_end = end + 1;
        if PROFILER_LIKELY (new_end - end_block_->start == Block::kNumSlots)
        {
            auto* new_block = new Block{/*start=*/new_end, /*next=*/nullptr, /*slots=*/{}};
            end_block_      = (end_block_->next = new_block);
            block_count_.fetch_add(1, std::memory_order_relaxed);
        }
        set_end(new_end);  // Write index after contents.
        return true;
    }

    // Producer-thread-only counter of push() calls dropped due to max_blocks_.
    // Relaxed: this is a diagnostic count, not a synchronization point -- the
    // pre-existing start_/end_ protocol above is what actually orders the
    // stored elements between producer and consumer.
    uint64_t dropped_count() const { return dropped_count_.load(std::memory_order_relaxed); }

    // Removes all elements from the queue.
    void clear()
    {
        size_t end = get_end();
        while (start_ != end)
        {
            pop_impl();
        }
    }

    // Removes one element off the front of the queue and returns it.
    std::optional<T> pop()
    {
        std::optional<T> element;
        size_t           end = get_end();
        if (start_ != end)
        {
            element = pop_impl();
        }
        return element;
    }

protected:
    void set_end(size_t end) { end_.set(end); }

    size_t get_end() const { return end_.get(); }

    // Returns true if the queue is empty.
    bool empty() const { return (start_ == get_end()); }

    // Removes one element off the front of the queue and returns it.
    // REQUIRES: The queue must not be empty.
    T pop_impl()
    {
        // PROFILER_CHECK_DEBUG(!empty(), "Queue is empty in pop_impl");

        // Move the next element into the output.
        auto& slot    = start_block_->slots[start_++ - start_block_->start];
        T     element = std::move(slot).consume();
        // If we reach the end of a block, we own it and should delete it.
        // The next block is present: end_ always points to something -- push()
        // never lets a block fill without linking a successor (see push()).
        if PROFILER_UNLIKELY (start_ - start_block_->start == Block::kNumSlots)
        {
            auto* old_block = std::exchange(start_block_, start_block_->next);
            delete old_block;
            block_count_.fetch_sub(1, std::memory_order_relaxed);
            // PROFILER_CHECK_DEBUG(
            // start_ == start_block_->start, "start_ is not equal to start_block_->start");
        }
        return element;
    }

    size_t                max_blocks_;       // Set at construction; never changes.
    std::atomic<size_t>   block_count_{1};   // Live blocks; constructor allocates the first.
    std::atomic<uint64_t> dropped_count_{0}; // See dropped_count().
    Block*                start_block_;      // Head: updated only by consumer thread.
    size_t                start_;            // Non-atomic: read only by consumer thread.
    Block*                end_block_;        // Tail: updated only by producer thread.
    Index<kAtomicEnd>     end_;              // Maybe atomic: read also by consumer thread.
};

}  // namespace QueueBaseInternal

template <typename T, size_t kBlockSize>
class LockFreeQueue;

template <typename T, size_t kBlockSize = 1 << 16 /* 64 KiB */>
class BlockedQueue final : public QueueBaseInternal::blocked_queue_base<T, kBlockSize, false>
{
    using Block = QueueBaseInternal::InternalBlock<T, kBlockSize>;
    friend class LockFreeQueue<T, kBlockSize>;

public:
    BlockedQueue() = default;

    explicit BlockedQueue(size_t max_blocks)
        : QueueBaseInternal::blocked_queue_base<T, kBlockSize, false>(max_blocks)
    {
    }

    BlockedQueue(BlockedQueue&& src) { *this = std::move(src); }

    BlockedQueue& operator=(BlockedQueue&& src)
    {
        this->clear();
        std::swap(this->max_blocks_, src.max_blocks_);
        QueueBaseInternal::swap_atomic(this->block_count_, src.block_count_);
        QueueBaseInternal::swap_atomic(this->dropped_count_, src.dropped_count_);
        std::swap(this->start_block_, src.start_block_);
        std::swap(this->start_, src.start_);
        std::swap(this->end_block_, src.end_block_);
        auto origin_end = this->get_end();
        this->set_end(src.get_end());
        src.set_end(origin_end);
        return *this;
    }

    class Iterator
    {
    public:
        bool operator==(const Iterator& another) const
        {
            return (index_ == another.index_) && (queue_ == another.queue_);
        }

        bool operator!=(const Iterator& another) const { return !(*this == another); }

        T& operator*() const
        {
            // PROFILER_CHECK_DEBUG(block_ != nullptr, "block_ is nullptr");
            // PROFILER_CHECK_DEBUG(
            // index_ >= block_->start,
            // "index_ {} is less than block_->start {}",
            // index_,
            // block_->start);
            // PROFILER_CHECK_DEBUG(
            // index_ < block_->start + Block::kNumSlots,
            // "index_ {} is greater than block_->start{} + Block::kNumSlots{}",
            // index_,
            // block_->start,
            // Block::kNumSlots);
            // PROFILER_CHECK_DEBUG(
            // index_ < queue_->End(),
            // "index_={} is greater than queue_->End()={}",
            // index_,
            // queue_->End());
            return block_->slots[index_ - block_->start].value;
        }

        T* operator->() const { return &(this->operator*()); }

        Iterator& operator++()
        {
            // PROFILER_CHECK_DEBUG(queue_ != nullptr, "queue_ is nullptr");
            // PROFILER_CHECK_DEBUG(block_ != nullptr, "block_ is nullptr");
            if (index_ < queue_->get_end())
            {
                ++index_;
                auto next_block_start = block_->start + Block::kNumSlots;
                // PROFILER_CHECK_DEBUG(
                // index_ < next_block_start,
                // "index_ {} is greater than next_block_start {}",
                // index_,
                // next_block_start);
                if (index_ == next_block_start)
                {
                    block_ = block_->next;
                    // PROFILER_CHECK_DEBUG(block_ != nullptr, "block_ is nullptr");
                }
            }
            return (*this);
        }

        Iterator operator++(int)
        {
            auto temp(*this);
            this->operator++();
            return temp;
        }

    private:
        friend class BlockedQueue;
        Iterator(BlockedQueue* queue, BlockedQueue::Block* block, size_t index)
            : queue_(queue), block_(block), index_(index) {};
        BlockedQueue*        queue_ = nullptr;
        BlockedQueue::Block* block_ = nullptr;
        size_t               index_ = 0;
    };

    Iterator begin() { return Iterator(this, this->start_block_, this->start_); }

    Iterator end() { return Iterator(this, this->end_block_, this->get_end()); }
};

template <typename T, size_t kBlockSize = 1 << 16 /* 64 KiB */>
class LockFreeQueue final : public QueueBaseInternal::blocked_queue_base<T, kBlockSize, true>
{
    using Block = QueueBaseInternal::InternalBlock<T, kBlockSize>;

public:
    LockFreeQueue() = default;

    explicit LockFreeQueue(size_t max_blocks)
        : QueueBaseInternal::blocked_queue_base<T, kBlockSize, true>(max_blocks)
    {
    }

    // Pop all events into an normal block storage queue, blocks are directly
    // moved into new queue except the last block. Those events
    // that are in the last block are in fact copied one by one.
    BlockedQueue<T, kBlockSize> PopAll()
    {
        BlockedQueue<T, kBlockSize> result;
        auto*                       empty_block = result.start_block_;
        result.start_block_ = result.end_block_ = nullptr;
        result.start_                           = this->start_;
        // Use the end we see now, skip further growing if any in another thread
        size_t end = this->get_end();
        result.set_end(end);
        while (this->start_block_->start + Block::kNumSlots <= end)
        {
            auto* old_block = std::exchange(this->start_block_, this->start_block_->next);
            this->start_    = this->start_block_->start;
            old_block->next = nullptr;
            this->block_count_.fetch_sub(1, std::memory_order_relaxed);
            if (result.end_block_)
            {
                result.end_block_->next = old_block;
            }
            else
            {
                result.start_block_ = old_block;
            }
            result.end_block_ = old_block;
        }
        empty_block->start = this->start_block_->start;
        if (result.end_block_ == nullptr)
        {
            result.end_block_ = result.start_block_ = empty_block;
        }
        else
        {
            result.end_block_->next = empty_block;
            result.end_block_       = empty_block;
        }
        size_t bs = this->start_block_->start;
        for (size_t i = std::max(this->start_, bs); i < end; i++)
        {
            auto& src_slot = this->start_block_->slots[i - bs];
            auto& dst_slot = result.end_block_->slots[i - bs];
            dst_slot.Emplace(std::move(src_slot).Consume());
        }
        this->start_ = end;
        return result;
    }
};

}  // namespace profiler
