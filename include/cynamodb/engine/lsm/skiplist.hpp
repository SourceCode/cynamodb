#pragma once
#include <atomic>
#include <string>
#include <vector>
#include <memory>
#include <map>
#include <random>
#include <algorithm>
#include <optional>
#include <limits>
#include <mutex>
#include <shared_mutex>
#include <cynamodb/core/types.hpp>
namespace cynamodb::engine::lsm {
constexpr int kSkiplistMaxLevel = 16;
constexpr float kSkiplistProbability = 0.5F;
constexpr size_t kMaxSkiplistKeyBytes = 1024U * 1024U;
constexpr size_t kMaxSkiplistEntries = 2000000U;
struct SkiplistNode {
    std::string key;
    std::map<std::string, std::shared_ptr<core::AttributeValue>, core::StringViewLess> attributes;
    std::vector<std::atomic<SkiplistNode*>> forward;
    bool is_deleted;
    SkiplistNode(const std::string& k, const std::map<std::string, std::shared_ptr<core::AttributeValue>, core::StringViewLess>& attrs, int level, bool deleted = false)
        : key(k), attributes(attrs), forward(level), is_deleted(deleted) {
        for (int i = 0; i < level; ++i) forward[i].store(nullptr, std::memory_order_relaxed);
    }
};
class Skiplist {
public:
    using Attributes = std::map<std::string, std::shared_ptr<core::AttributeValue>, core::StringViewLess>;
    struct SnapshotEntry { Attributes attributes; bool is_deleted = false; };
    Skiplist() {
        head_ = new SkiplistNode("", {}, kSkiplistMaxLevel);
        level_.store(1, std::memory_order_relaxed);
    }
    ~Skiplist() {
        std::unique_lock lock(mutex_);
        SkiplistNode* curr = head_;
        while (curr != nullptr) { SkiplistNode* next = curr->forward[0].load(std::memory_order_relaxed); delete curr; curr = next; }
    }
    std::optional<Attributes> get(const std::string& key) const {
        if (!is_valid_user_key(key)) return std::nullopt;
        std::shared_lock lock(mutex_);
        SkiplistNode* curr = head_;
        const int current_level = std::clamp(level_.load(std::memory_order_acquire), 1, kSkiplistMaxLevel);
        for (int i = current_level - 1; i >= 0; --i) {
            SkiplistNode* next = curr->forward[i].load(std::memory_order_acquire);
            while (next != nullptr && next->key < key) { curr = next; next = curr->forward[i].load(std::memory_order_acquire); }
        }
        curr = curr->forward[0].load(std::memory_order_acquire);
        if (curr != nullptr && curr->key == key) { if (curr->is_deleted) return std::nullopt; return curr->attributes; }
        return std::nullopt;
    }
    bool is_tombstoned(const std::string& key) const {
        if (!is_valid_user_key(key)) return false;
        std::shared_lock lock(mutex_);
        SkiplistNode* curr = head_;
        const int current_level = std::clamp(level_.load(std::memory_order_acquire), 1, kSkiplistMaxLevel);
        for (int i = current_level - 1; i >= 0; --i) {
            SkiplistNode* next = curr->forward[i].load(std::memory_order_acquire);
            while (next != nullptr && next->key < key) { curr = next; next = curr->forward[i].load(std::memory_order_acquire); }
        }
        curr = curr->forward[0].load(std::memory_order_acquire);
        return curr != nullptr && curr->key == key && curr->is_deleted;
    }
    std::map<std::string, Attributes, core::StringViewLess> get_all() const {
        std::shared_lock lock(mutex_);
        std::map<std::string, Attributes, core::StringViewLess> result;
        SkiplistNode* curr = head_->forward[0].load(std::memory_order_acquire);
        while (curr != nullptr) { if (!curr->is_deleted) result[curr->key] = curr->attributes; curr = curr->forward[0].load(std::memory_order_acquire); }
        return result;
    }
    std::map<std::string, SnapshotEntry, core::StringViewLess> get_all_entries() const {
        std::shared_lock lock(mutex_);
        std::map<std::string, SnapshotEntry, core::StringViewLess> result;
        SkiplistNode* curr = head_->forward[0].load(std::memory_order_acquire);
        while (curr != nullptr) { result[curr->key] = SnapshotEntry{curr->attributes, curr->is_deleted}; curr = curr->forward[0].load(std::memory_order_acquire); }
        return result;
    }
    void insert(const std::string& key, const std::map<std::string, std::shared_ptr<core::AttributeValue>, core::StringViewLess>& attributes, bool is_deleted = false) {
        if (!is_valid_user_key(key)) return;
        std::unique_lock lock(mutex_);
        std::vector<SkiplistNode*> update(kSkiplistMaxLevel, nullptr);
        SkiplistNode* curr = head_;
        const int current_level = std::clamp(level_.load(std::memory_order_relaxed), 1, kSkiplistMaxLevel);
        for (int i = current_level - 1; i >= 0; --i) {
            SkiplistNode* next = curr->forward[i].load(std::memory_order_relaxed);
            while (next != nullptr && next->key < key) { curr = next; next = curr->forward[i].load(std::memory_order_relaxed); }
            update[i] = curr;
        }
        curr = curr->forward[0].load(std::memory_order_relaxed);
        if (curr != nullptr && curr->key == key) { curr->attributes = attributes; curr->is_deleted = is_deleted; }
        else {
            if (size_.load(std::memory_order_relaxed) >= kMaxSkiplistEntries) return;
            int new_level = random_level();
            if (new_level > current_level) { for (int i = current_level; i < new_level; ++i) update[i] = head_; level_.store(new_level, std::memory_order_relaxed); }
            SkiplistNode* new_node = new SkiplistNode(key, attributes, new_level, is_deleted);
            for (int i = 0; i < new_level; ++i) {
                new_node->forward[i].store(update[i]->forward[i].load(std::memory_order_relaxed), std::memory_order_relaxed);
                update[i]->forward[i].store(new_node, std::memory_order_release);
            }
            size_.fetch_add(1, std::memory_order_relaxed);
        }
    }
    size_t size() const { return size_.load(std::memory_order_relaxed); }
private:
    int random_level() {
        thread_local std::mt19937 rng{std::random_device{}()};
        thread_local std::uniform_real_distribution<float> dist(0.0F, 1.0F);
        int level = 1;
        while (dist(rng) < kSkiplistProbability && level < kSkiplistMaxLevel) level++;
        return level;
    }
    static bool is_valid_user_key(const std::string& key) { return !key.empty() && key.size() <= kMaxSkiplistKeyBytes; }
    SkiplistNode* head_;
    std::atomic<int> level_;
    std::atomic<size_t> size_{0};
    mutable std::shared_mutex mutex_;
};
}
