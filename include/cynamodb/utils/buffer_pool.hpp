#pragma once
#include <span>
#include <vector>
#include <memory>
namespace cynamodb::utils {
class BufferPool {
public:
    enum class SizeClass { Small = 64 * 1024, Medium = 256 * 1024, Large = 1024 * 1024 };
    struct Buffer { std::unique_ptr<char[]> data; size_t size; std::span<char> span() { return {data.get(), size}; } };
    static BufferPool& get_thread_local_pool();
    Buffer get_buffer(SizeClass size_class);
    void return_buffer(Buffer buffer);
private:
    std::vector<Buffer> small_buffers_;
    std::vector<Buffer> medium_buffers_;
    std::vector<Buffer> large_buffers_;
};
class PooledBuffer {
public:
    PooledBuffer(BufferPool::SizeClass size_class) { buffer_ = BufferPool::get_thread_local_pool().get_buffer(size_class); }
    ~PooledBuffer() { if (buffer_.data) BufferPool::get_thread_local_pool().return_buffer(std::move(buffer_)); }
    PooledBuffer(const PooledBuffer&) = delete;
    PooledBuffer& operator=(const PooledBuffer&) = delete;
    PooledBuffer(PooledBuffer&& other) noexcept : buffer_(std::move(other.buffer_)) { other.buffer_.data = nullptr; }
    PooledBuffer& operator=(PooledBuffer&& other) noexcept {
        if (this != &other) {
            if (buffer_.data) BufferPool::get_thread_local_pool().return_buffer(std::move(buffer_));
            buffer_ = std::move(other.buffer_);
            other.buffer_.data = nullptr;
        }
        return *this;
    }
    std::span<char> span() { return buffer_.span(); }
private:
    BufferPool::Buffer buffer_;
};
}
