#pragma once

#include <vector>
#include <string>
#include <mutex>
#include <memory>
#include <optional>
#include <unordered_map>
#include <list>

namespace cynamodb::engine::lsm {

class Block {
public:
    explicit Block(std::string data) : data_(std::move(data)) {}
    const std::string& data() const { return data_; }
    size_t size() const { return data_.size(); }

private:
    std::string data_;
};

struct CacheHandle {
    std::shared_ptr<Block> block;
};

class CacheShard {
public:
    explicit CacheShard(size_t capacity_bytes) : capacity_(capacity_bytes), current_size_(0) {}

    void insert(const std::string& key, std::shared_ptr<Block> block) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (block->size() > capacity_) return;

        auto it = cache_.find(key);
        if (it != cache_.end()) {
            current_size_ -= it->second.first->size();
            lru_list_.erase(it->second.second);
            cache_.erase(it);
        }

        while (current_size_ + block->size() > capacity_ && !lru_list_.empty()) {
            auto last_key = lru_list_.back();
            auto last_it = cache_.find(last_key);
            if (last_it != cache_.end()) {
                current_size_ -= last_it->second.first->size();
                cache_.erase(last_it);
            }
            lru_list_.pop_back();
        }

        lru_list_.push_front(key);
        cache_[key] = {block, lru_list_.begin()};
        current_size_ += block->size();
    }

    std::optional<CacheHandle> get(const std::string& key) {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = cache_.find(key);
        if (it == cache_.end()) return std::nullopt;

        lru_list_.erase(it->second.second);
        lru_list_.push_front(key);
        it->second.second = lru_list_.begin();
        return CacheHandle{it->second.first};
    }

private:
    size_t capacity_;
    size_t current_size_;
    std::unordered_map<std::string, std::pair<std::shared_ptr<Block>, std::list<std::string>::iterator>> cache_;
    std::list<std::string> lru_list_;
    std::mutex mutex_;
};

class ShardedBlockCache {
public:
    explicit ShardedBlockCache(size_t total_capacity_bytes, size_t num_shards = 64) {
        if (num_shards == 0) num_shards = 1;
        size_t shard_capacity = total_capacity_bytes / num_shards;
        for (size_t i = 0; i < num_shards; ++i) {
            shards_.push_back(std::make_unique<CacheShard>(shard_capacity));
        }
    }

    void insert(const std::string& key, std::shared_ptr<Block> block) {
        get_shard(key).insert(key, block);
    }

    std::optional<CacheHandle> get(const std::string& key) {
        return get_shard(key).get(key);
    }

private:
    CacheShard& get_shard(const std::string& key) {
        size_t h = std::hash<std::string>{}(key);
        return *shards_[h % shards_.size()];
    }

    std::vector<std::unique_ptr<CacheShard>> shards_;
};

} // namespace cynamodb::engine::lsm
