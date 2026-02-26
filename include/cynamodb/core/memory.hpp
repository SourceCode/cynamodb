#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <new>
#include <stdexcept>
#include <vector>
#include <memory>
#include <mutex>

namespace cynamodb::core {

class Arena {
public:
    static constexpr size_t kDefaultBlockSize = 4096;
    static constexpr size_t kMaxBlockSize = 64U * 1024U * 1024U;
    static constexpr size_t kMaxAllocationBytes = 64U * 1024U * 1024U;
    static constexpr size_t kMaxBlocks = 1U * 1024U * 1024U;

    explicit Arena(size_t block_size = 4096)
        : block_size_(std::clamp(block_size, alignof(std::max_align_t), kMaxBlockSize)) {
        allocate_block();
    }

    Arena(const Arena&) = delete;
    Arena& operator=(const Arena&) = delete;

    void* allocate(size_t size, size_t alignment = alignof(std::max_align_t)) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (size == 0) {
            size = 1;
        }
        if (alignment == 0 || (alignment & (alignment - 1)) != 0) {
            throw std::invalid_argument("Arena alignment must be a power of two");
        }
        if (alignment > alignof(std::max_align_t)) {
            throw std::invalid_argument("Arena alignment exceeds supported max alignment");
        }
        if (size > kMaxAllocationBytes) {
            throw std::bad_alloc();
        }
        if (size > block_size_) {
            return allocate_large_block_locked(size);
        }

        size_t aligned_offset = align_up(current_offset_, alignment);
        if (aligned_offset > block_size_ || size > (block_size_ - aligned_offset)) {
            allocate_block();
            aligned_offset = align_up(current_offset_, alignment);
        }

        if (aligned_offset > block_size_ || size > (block_size_ - aligned_offset)) {
            throw std::bad_alloc();
        }

        current_offset_ = aligned_offset;
        if (current_offset_ + size > block_size_) {
            throw std::bad_alloc();
        }
        void* result = current_block_ + current_offset_;
        current_offset_ += size;
        bytes_allocated_ = saturating_add(bytes_allocated_, size);
        return result;
    }

    template<typename T, typename... Args>
    T* construct(Args&&... args) {
        void* mem = allocate(sizeof(T), alignof(T));
        return new (mem) T(std::forward<Args>(args)...);
    }

    void reset() {
        std::lock_guard<std::mutex> lock(mutex_);
        if (blocks_.empty()) {
            allocate_block();
            return;
        }
        current_block_idx_ = 0;
        current_block_ = blocks_[0].get();
        current_offset_ = 0;
        bytes_allocated_ = 0;
    }

    size_t bytes_allocated() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return bytes_allocated_;
    }

    size_t block_count() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return blocks_.size();
    }

private:
    static size_t saturating_add(size_t a, size_t b) {
        if (std::numeric_limits<size_t>::max() - a < b) {
            return std::numeric_limits<size_t>::max();
        }
        return a + b;
    }

    static size_t align_up(size_t value, size_t alignment) {
        const size_t mask = alignment - 1;
        if (value > std::numeric_limits<size_t>::max() - mask) {
            throw std::bad_alloc();
        }
        return (value + mask) & ~mask;
    }

    void* allocate_large_block_locked(size_t size) {
        if (blocks_.size() >= kMaxBlocks) {
            throw std::bad_alloc();
        }
        const size_t block_bytes = std::max(size, alignof(std::max_align_t));
        auto block = std::make_unique<uint8_t[]>(block_bytes);
        void* result = block.get();
        blocks_.push_back(std::move(block));
        current_block_idx_ = blocks_.size() - 1;
        current_block_ = blocks_[current_block_idx_].get();
        current_offset_ = size;
        bytes_allocated_ = saturating_add(bytes_allocated_, size);
        return result;
    }

    void allocate_block() {
        if (current_block_idx_ + 1 < blocks_.size()) {
            current_block_idx_++;
            current_block_ = blocks_[current_block_idx_].get();
            current_offset_ = 0;
            return;
        }
        if (blocks_.size() >= kMaxBlocks) {
            throw std::bad_alloc();
        }

        auto block = std::make_unique<uint8_t[]>(block_size_);
        current_block_ = block.get();
        blocks_.push_back(std::move(block));
        current_block_idx_ = blocks_.size() - 1;
        current_offset_ = 0;
    }

    size_t block_size_;
    std::vector<std::unique_ptr<uint8_t[]>> blocks_;
    uint8_t* current_block_ = nullptr;
    size_t current_block_idx_ = 0;
    size_t current_offset_ = 0;
    size_t bytes_allocated_ = 0;
    mutable std::mutex mutex_;
};

} // namespace cynamodb::core
