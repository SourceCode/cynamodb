#include <cynamodb/utils/buffer_pool.hpp>
#include <simdjson.h>
#include <cstdlib>

namespace cynamodb::utils {

BufferPool& BufferPool::get_thread_local_pool() {
    thread_local BufferPool pool;
    return pool;
}

BufferPool::Buffer BufferPool::get_buffer(SizeClass size_class) {
    auto pop_or_allocate = [](std::vector<Buffer>& buffers, size_t size) -> Buffer {
        if (!buffers.empty()) {
            Buffer buf = std::move(buffers.back());
            buffers.pop_back();
            return buf;
        }
        size_t alloc_size = size + simdjson::SIMDJSON_PADDING;
        auto data = std::make_unique_for_overwrite<char[]>(alloc_size);
        return {std::move(data), size};
    };

    switch (size_class) {
        case SizeClass::Small:
            return pop_or_allocate(small_buffers_, static_cast<size_t>(SizeClass::Small));
        case SizeClass::Medium:
            return pop_or_allocate(medium_buffers_, static_cast<size_t>(SizeClass::Medium));
        case SizeClass::Large:
            return pop_or_allocate(large_buffers_, static_cast<size_t>(SizeClass::Large));
        default:
            return pop_or_allocate(large_buffers_, static_cast<size_t>(SizeClass::Large));
    }
}

void BufferPool::return_buffer(Buffer buffer) {
    if (!buffer.data) return;

    if (buffer.size == static_cast<size_t>(SizeClass::Small)) {
        small_buffers_.push_back(std::move(buffer));
    } else if (buffer.size == static_cast<size_t>(SizeClass::Medium)) {
        medium_buffers_.push_back(std::move(buffer));
    } else if (buffer.size == static_cast<size_t>(SizeClass::Large)) {
        large_buffers_.push_back(std::move(buffer));
    }
}

} // namespace cynamodb::utils
