#pragma once

#include <functional>
#include <string>
#include <map>
#include <memory>
#include <optional>
#include <vector>
#include <cynamodb/core/sizing.hpp>
#include <cynamodb/core/types.hpp>

namespace cynamodb::engine {

class StorageEngine {
public:
    using AttributeMap = std::map<std::string, std::shared_ptr<core::AttributeValue>, core::StringViewLess>;
    using Item = AttributeMap;
    static constexpr size_t kMaxReadPageBytes = 1024U * 1024U;

    static size_t item_size_bytes(const AttributeMap& item) {
        size_t size = 0;
        for (const auto& [name, value] : item) {
            size = core::size_saturating_add(size, name.size());
            if (value) size = core::size_saturating_add(size, core::attribute_size(*value));
        }
        return size;
    }

    struct ScanResult {
        std::vector<AttributeMap> items;
        std::optional<std::string> last_evaluated_key;
    };

    struct QueryResult {
        std::vector<AttributeMap> items;
        std::optional<std::string> last_evaluated_key;
    };

    // The write a Mutator asks the engine to perform once it has seen the current
    // value. None aborts the mutation (e.g. a failed ConditionExpression).
    enum class MutationKind { None, Put, Delete };
    struct Mutation {
        MutationKind kind = MutationKind::None;
        AttributeMap attributes;  // used when kind == Put
    };

    // Receives the current item (nullptr if absent) and returns the write to apply.
    using Mutator = std::function<Mutation(const AttributeMap* current)>;

    struct MutationOutcome {
        bool applied = false;                  // false if the mutator returned None
        std::optional<AttributeMap> previous;  // item as it was before the write
    };

    virtual ~StorageEngine() = default;

    virtual void put(const std::string& table_name, const std::string& key, const AttributeMap& attributes) = 0;
    virtual void remove(const std::string& table_name, const std::string& key) = 0;
    virtual std::optional<AttributeMap> get(const std::string& table_name, const std::string& key) = 0;

    // Atomic read-modify-write performed under the engine's write lock, so the
    // current value seen by `mutator` cannot change before its decision is applied.
    // This is the foundation for conditional writes and UpdateItem.
    virtual MutationOutcome mutate(const std::string& table_name, const std::string& key,
                                   const Mutator& mutator) = 0;

    // Drops every item belonging to a table. Called when a table is deleted.
    virtual void drop_table(const std::string& table_name) = 0;

    virtual ScanResult scan(const std::string& table_name, const std::optional<std::string>& exclusive_start_key, size_t limit) = 0;
    virtual QueryResult query(
        const std::string& table_name,
        const AttributeMap& key_conditions,
        const std::optional<std::string>& exclusive_start_key,
        size_t limit,
        const std::optional<std::string>& key_prefix = std::nullopt,
        bool scan_forward = true) = 0;
};

} // namespace cynamodb::engine
