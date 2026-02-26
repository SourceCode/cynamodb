#pragma once
#include <memory_resource>
#include <atomic>
#include <cstddef>
#include <array>
#include <mutex>
#include <cstdlib>
#include <algorithm>
namespace cynamodb::core {
class TrackingMemoryResource : public std::pmr::memory_resource {
public:
    TrackingMemoryResource() : upstream_(std::pmr::new_delete_resource()), current_usage_(0), peak_usage_(0) {}
    explicit TrackingMemoryResource(std::pmr::memory_resource* upstream) : upstream_(upstream), current_usage_(0), peak_usage_(0) {}
    size_t current_usage() const { return current_usage_.load(std::memory_order_relaxed); }
    size_t peak_usage() const { return peak_usage_.load(std::memory_order_relaxed); }
protected:
    void* do_allocate(size_t bytes, size_t alignment) override {
        void* ptr = upstream_->allocate(bytes, alignment);
        size_t prev = current_usage_.fetch_add(bytes, std::memory_order_relaxed);
        size_t new_val = prev + bytes;
        size_t peak = peak_usage_.load(std::memory_order_relaxed);
        while (new_val > peak && !peak_usage_.compare_exchange_weak(peak, new_val, std::memory_order_relaxed)) {}
        return ptr;
    }
    void do_deallocate(void* p, size_t bytes, size_t alignment) override {
        current_usage_.fetch_sub(bytes, std::memory_order_relaxed);
        upstream_->deallocate(p, bytes, alignment);
    }
    bool do_is_equal(const std::pmr::memory_resource& other) const noexcept override { return this == &other; }
private:
    std::pmr::memory_resource* upstream_;
    std::atomic<size_t> current_usage_;
    std::atomic<size_t> peak_usage_;
};
class AlignedMemoryResource : public std::pmr::memory_resource {
public:
    AlignedMemoryResource() = default;
protected:
    void* do_allocate(size_t bytes, size_t alignment) override {
        size_t final_alignment = std::max(alignment, static_cast<size_t>(64));
        void* ptr = nullptr;
        if (posix_memalign(&ptr, final_alignment, bytes) != 0) throw std::bad_alloc();
        return ptr;
    }
    void do_deallocate(void* p, [[maybe_unused]] size_t bytes, [[maybe_unused]] size_t alignment) override { std::free(p); }
    bool do_is_equal(const std::pmr::memory_resource& other) const noexcept override { return this == &other; }
};
class MemoryManager {
public:
    static TrackingMemoryResource& global_resource();
    static void initialize();
    static double get_fragmentation_ratio();
    static void on_memory_pressure();
};
template <size_t Size>
class TrivialArena {
public:
    TrivialArena() : resource_(buffer_.data(), Size, std::pmr::null_memory_resource()) {}
    std::pmr::memory_resource* resource() { return &resource_; }
private:
    std::array<std::byte, Size> buffer_;
    std::pmr::monotonic_buffer_resource resource_;
};
template <typename T>
void pmr_delete(std::pmr::memory_resource* res, T* ptr) {
    if (ptr) { ptr->~T(); res->deallocate(ptr, sizeof(T), alignof(T)); }
}
}
