#pragma once

#include <string>
#include <map>
#include <memory>
#include <optional>
#include <vector>
#include <cynamodb/core/types.hpp>

namespace cynamodb::engine {

class StorageEngine {
public:
    using AttributeMap = std::map<std::string, std::shared_ptr<core::AttributeValue>, core::StringViewLess>;
    using Item = AttributeMap;

    struct ScanResult {
        std::vector<AttributeMap> items;
        std::optional<std::string> last_evaluated_key;
    };

    struct QueryResult {
        std::vector<AttributeMap> items;
        std::optional<std::string> last_evaluated_key;
    };

    virtual ~StorageEngine() = default;

    virtual void put(const std::string& table_name, const std::string& key, const AttributeMap& attributes) = 0;
    virtual void remove(const std::string& table_name, const std::string& key) = 0;
    virtual std::optional<AttributeMap> get(const std::string& table_name, const std::string& key) = 0;
    
    virtual ScanResult scan(const std::string& table_name, const std::optional<std::string>& exclusive_start_key, size_t limit) = 0;
    virtual QueryResult query(const std::string& table_name, const AttributeMap& key_conditions, const std::optional<std::string>& exclusive_start_key, size_t limit) = 0;
};

} // namespace cynamodb::engine
