#pragma once
#include <atomic>
#include <cstddef>

// Lock-free Single-Producer Single-Consumer ring buffer.
// Head and tail are placed on separate cache lines to eliminate
// false sharing between the producer and consumer cores.
template<typename T, size_t Size>
class SPSCRingBuffer
{
    static_assert((Size & (Size - 1)) == 0, "Size must be a power of 2");
    static_assert(Size >= 2, "Size must be at least 2");

    static constexpr size_t MASK = Size - 1;

private:
    T buffer[Size];

    alignas(64) std::atomic<size_t> head{0};  // producer writes here
    alignas(64) std::atomic<size_t> tail{0};  // consumer reads here

public:
    bool push(const T& item)
    {
        size_t current_head = head.load(std::memory_order_relaxed);
        size_t next_head = (current_head + 1) & MASK;

        if (next_head == tail.load(std::memory_order_acquire))
        {
            return false;  // queue full
        }

        buffer[current_head] = item;
        head.store(next_head, std::memory_order_release);
        return true;
    }

    bool pop(T& item)
    {
        size_t current_tail = tail.load(std::memory_order_relaxed);

        if (current_tail == head.load(std::memory_order_acquire))
        {
            return false;  // queue empty
        }

        item = buffer[current_tail];
        tail.store((current_tail + 1) & MASK, std::memory_order_release);
        return true;
    }

    size_t size() const
    {
        size_t h = head.load(std::memory_order_acquire);
        size_t t = tail.load(std::memory_order_acquire);
        return (h - t) & MASK;
    }

    bool empty() const
    {
        return head.load(std::memory_order_acquire) == tail.load(std::memory_order_acquire);
    }
};
