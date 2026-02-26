#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <vector>
#include <memory>
#include <cynamodb/engine/storage_engine.hpp>

namespace cynamodb::engine::transactions {

struct VersionedItem {
    StorageEngine::AttributeMap data;
    uint64_t _ts;
    uint64_t _tid;
};

struct TransactionContext {
    uint64_t tid;
    uint64_t start_ts;
    
    // key: table_name + ":" + encoded_key
    std::map<std::string, uint64_t> read_set;
    std::map<std::string, StorageEngine::AttributeMap> write_set;
};

} // namespace cynamodb::engine::transactions
