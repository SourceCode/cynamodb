#include <cynamodb/engine/memory_engine.hpp>
#include <cynamodb/engine/key_codec.hpp>
#include <algorithm>
#include <variant>

namespace cynamodb::engine {

namespace {

using core::AttributeType;
using core::AttributeValue;

// Equality comparison for the attribute types that can appear in key conditions.
bool attribute_equals(const AttributeValue& a, const AttributeValue& b) {
    if (a.type != b.type) {
        return false;
    }
    switch (a.type) {
        case AttributeType::S:
        case AttributeType::N:
            return std::get<core::String>(a.value) == std::get<core::String>(b.value);
        case AttributeType::B:
            return std::get<std::pmr::vector<uint8_t>>(a.value) ==
                   std::get<std::pmr::vector<uint8_t>>(b.value);
        case AttributeType::BOOL:
            return std::get<bool>(a.value) == std::get<bool>(b.value);
        case AttributeType::NUL:
            return true;  // both NULL
        default:
            return false;  // sets / maps / lists are not valid key conditions
    }
}

bool item_matches_conditions(const StorageEngine::AttributeMap& item,
                             const StorageEngine::AttributeMap& conditions) {
    for (const auto& [name, expected] : conditions) {
        if (!expected) {
            return false;
        }
        auto it = item.find(name);
        if (it == item.end() || !it->second || !attribute_equals(*it->second, *expected)) {
            return false;
        }
    }
    return true;
}

std::optional<std::string> prefix_successor(std::string prefix) {
    for (size_t i = prefix.size(); i > 0; --i) {
        const auto byte = static_cast<unsigned char>(prefix[i - 1]);
        if (byte == 0xFF) continue;
        prefix[i - 1] = static_cast<char>(byte + 1);
        prefix.resize(i);
        return prefix;
    }
    return std::nullopt;
}

}  // namespace

void MemoryEngine::put(const std::string& table_name, const std::string& key, const AttributeMap& attributes) {
    std::unique_lock lock(mutex_);
    data_[table_name][key] = attributes;
}

void MemoryEngine::remove(const std::string& table_name, const std::string& key) {
    std::unique_lock lock(mutex_);
    auto table_it = data_.find(table_name);
    if (table_it != data_.end()) {
        table_it->second.erase(key);
    }
}

std::optional<StorageEngine::AttributeMap> MemoryEngine::get(const std::string& table_name, const std::string& key) {
    std::shared_lock lock(mutex_);
    auto table_it = data_.find(table_name);
    if (table_it == data_.end()) {
        return std::nullopt;
    }
    auto item_it = table_it->second.find(key);
    if (item_it == table_it->second.end()) {
        return std::nullopt;
    }
    return item_it->second;
}

MemoryEngine::MutationOutcome MemoryEngine::mutate(const std::string& table_name, const std::string& key, const Mutator& mutator) {
    std::unique_lock lock(mutex_);
    MutationOutcome outcome;
    auto& table = data_[table_name];
    auto it = table.find(key);
    const AttributeMap* current = (it != table.end()) ? &it->second : nullptr;
    if (current) outcome.previous = *current;

    Mutation m = mutator(current);
    switch (m.kind) {
        case MutationKind::Put:
            table[key] = std::move(m.attributes);
            outcome.applied = true;
            break;
        case MutationKind::Delete:
            if (it != table.end()) table.erase(it);
            outcome.applied = true;
            break;
        case MutationKind::None:
            break;
    }
    return outcome;
}

void MemoryEngine::drop_table(const std::string& table_name) {
    std::unique_lock lock(mutex_);
    data_.erase(table_name);
}

MemoryEngine::ScanResult MemoryEngine::scan(const std::string& table_name, const std::optional<std::string>& exclusive_start_key, size_t limit) {
    ScanResult result;
    std::shared_lock lock(mutex_);
    auto table_it = data_.find(table_name);
    if (table_it == data_.end()) {
        return result;
    }
    const auto& items = table_it->second;

    auto it = items.begin();
    if (exclusive_start_key) {
        it = items.upper_bound(*exclusive_start_key);
    }

    size_t page_bytes = 0;
    std::string last_key;
    for (; it != items.end(); ++it) {
        const size_t item_bytes = item_size_bytes(it->second);
        if (!result.items.empty() &&
            ((limit != 0 && result.items.size() == limit) ||
             item_bytes > kMaxReadPageBytes - std::min(page_bytes, kMaxReadPageBytes))) {
            // More items remain: report a pagination token so the caller can resume.
            result.last_evaluated_key = last_key;
            return result;
        }
        result.items.push_back(it->second);
        page_bytes = core::size_saturating_add(page_bytes, item_bytes);
        last_key = it->first;
    }
    return result;
}

MemoryEngine::QueryResult MemoryEngine::query(
    const std::string& table_name,
    const AttributeMap& key_conditions,
    const std::optional<std::string>& exclusive_start_key,
    size_t limit,
    const std::optional<std::string>& key_prefix,
    bool scan_forward) {
    QueryResult result;
    std::shared_lock lock(mutex_);
    auto table_it = data_.find(table_name);
    if (table_it == data_.end()) {
        return result;
    }
    const auto& items = table_it->second;

    std::string last_key;
    size_t page_bytes = 0;
    if (scan_forward) {
        auto it = key_prefix ? items.lower_bound(*key_prefix) : items.begin();
        if (exclusive_start_key) it = items.upper_bound(*exclusive_start_key);
        for (; it != items.end(); ++it) {
            if (key_prefix && it->first.compare(0, key_prefix->size(), *key_prefix) != 0) break;
            if (!item_matches_conditions(it->second, key_conditions)) continue;
            const size_t item_bytes = item_size_bytes(it->second);
            if (!result.items.empty() &&
                ((limit != 0 && result.items.size() == limit) ||
                 item_bytes > kMaxReadPageBytes - std::min(page_bytes, kMaxReadPageBytes))) {
                result.last_evaluated_key = last_key;
                break;
            }
            result.items.push_back(it->second);
            page_bytes = core::size_saturating_add(page_bytes, item_bytes);
            last_key = it->first;
        }
        return result;
    }

    auto it = exclusive_start_key ? items.lower_bound(*exclusive_start_key) : items.end();
    if (!exclusive_start_key && key_prefix) {
        if (auto upper = prefix_successor(*key_prefix)) it = items.lower_bound(*upper);
    }
    while (it != items.begin()) {
        --it;
        if (key_prefix && it->first.compare(0, key_prefix->size(), *key_prefix) != 0) break;
        if (!item_matches_conditions(it->second, key_conditions)) continue;
        const size_t item_bytes = item_size_bytes(it->second);
        if (!result.items.empty() &&
            ((limit != 0 && result.items.size() == limit) ||
             item_bytes > kMaxReadPageBytes - std::min(page_bytes, kMaxReadPageBytes))) {
            result.last_evaluated_key = last_key;
            break;
        }
        result.items.push_back(it->second);
        page_bytes = core::size_saturating_add(page_bytes, item_bytes);
        last_key = it->first;
    }
    return result;
}

std::expected<void, StorageError> MemoryEngine::put_item(const core::TableDefinition& table_def, const Item& item) {
    std::string key = make_key(table_def, item);
    put(table_def.table_name, key, item);
    return {};
}

std::expected<MemoryEngine::Item, StorageError> MemoryEngine::get_item(const core::TableDefinition& table_def, const std::map<std::string, std::shared_ptr<core::AttributeValue>, core::StringViewLess>& key) {
    std::string encoded = make_key(table_def, key);
    auto value = get(table_def.table_name, encoded);
    if (!value) {
        return std::unexpected(StorageError::ItemNotFound);
    }
    return *value;
}

std::expected<void, StorageError> MemoryEngine::delete_item(const core::TableDefinition& table_def, const std::map<std::string, std::shared_ptr<core::AttributeValue>, core::StringViewLess>& key) {
    std::string encoded = make_key(table_def, key);
    remove(table_def.table_name, encoded);
    return {};
}

std::string MemoryEngine::make_key(const core::TableDefinition& table_def, const std::map<std::string, std::shared_ptr<core::AttributeValue>, core::StringViewLess>& key) {
    return encode_primary_key(table_def, key);
}

} // namespace cynamodb::engine
