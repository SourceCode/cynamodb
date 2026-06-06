#pragma once

#include <string>
#include <cynamodb/core/schema.hpp>
#include <cynamodb/core/types.hpp>
#include <cynamodb/engine/storage_engine.hpp>

namespace cynamodb::engine {

// Encodes a single attribute value order-preservingly and appends it to `out`.
void encode_key_component(std::string& out, const core::AttributeValue& av);

// Builds the order-preserving composite storage key for a table by extracting
// the key-schema attributes (partition then optional sort key) from `item`,
// which may be a full item or only the key attributes. Lexicographic comparison
// of the returned bytes matches DynamoDB's logical (partition, sort) ordering.
std::string encode_primary_key(const core::TableDefinition& def,
                               const StorageEngine::AttributeMap& item);

}  // namespace cynamodb::engine
