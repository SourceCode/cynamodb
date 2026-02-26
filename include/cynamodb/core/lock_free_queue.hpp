#pragma once

#include <atomic>
#include <vector>
#include <optional>
#include <cassert>

namespace cynamodb::core {

// Lock-Free MPMC Queue based on a Ring Buffer
template <typename T, size_t Capacity = 1024>
class LockFreeQueue {
    static_assert((Capacity & (Capacity - 1)) == 0, "Capacity must be a power of two");

public:
    LockFreeQueue() {
        for (size_t i = 0; i < Capacity; ++i) {
            buffer_[i].sequence.store(i, std::memory_order_relaxed);
        }
    }

    bool enqueue(T data) {
        size_t pos = enqueue_pos_.load(std::memory_order_relaxed);
        while (true) {
            auto& cell = buffer_[pos & mask_];
            size_t seq = cell.sequence.load(std::memory_order_acquire);
            intptr_t diff = static_cast<intptr_t>(seq) - static_cast<intptr_t>(pos);
            
            if (diff == 0) {
                if (enqueue_pos_.compare_exchange_weak(pos, pos + 1, std::memory_order_relaxed)) {
                    cell.data = std::move(data);
                    cell.sequence.store(pos + 1, std::memory_order_release);
                    return true;
                }
            } else if (diff < 0) {
                return false; // Full
            } else {
                pos = enqueue_pos_.load(std::memory_order_relaxed);
            }
        }
    }

    std::optional<T> dequeue() {
        size_t pos = dequeue_pos_.load(std::memory_order_relaxed);
        while (true) {
            auto& cell = buffer_[pos & mask_];
            size_t seq = cell.sequence.load(std::memory_order_acquire);
            intptr_t diff = static_cast<intptr_t>(seq) - static_cast<intptr_t>(pos + 1);
            
            if (diff == 0) {
                if (dequeue_pos_.compare_exchange_weak(pos, pos + 1, std::memory_order_relaxed)) {
                    T data = std::move(cell.data);
                    cell.sequence.store(pos + mask_ + 1, std::memory_order_release);
                    return data;
                }
            } else if (diff < 0) {
                return std::nullopt; // Empty
            } else {
                pos = dequeue_pos_.load(std::memory_order_relaxed);
            }
        }
    }

    size_t size() const {
        size_t ep = enqueue_pos_.load(std::memory_order_relaxed);
        size_t dp = dequeue_pos_.load(std::memory_order_relaxed);
        return ep > dp ? ep - dp : 0;
    }

private:
    struct alignas(64) Cell {
        std::atomic<size_t> sequence;
        T data;
    };

    static constexpr size_t mask_ = Capacity - 1;
    alignas(64) Cell buffer_[Capacity];
    alignas(64) std::atomic<size_t> enqueue_pos_{0};
    alignas(64) std::atomic<size_t> dequeue_pos_{0};
};

} // namespace cynamodb::core
