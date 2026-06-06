#pragma once

#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

#include <cynamodb/core/types.hpp>

namespace cynamodb::engine::lsm {

// Full-fidelity binary (de)serialization of a DynamoDB attribute map, used by the
// write-ahead log so that unflushed writes survive a restart with every attribute
// type intact (S, N, B, BOOL, NULL, M, L, SS, NS, BS).
using RecordAttributes = std::map<std::string, std::shared_ptr<core::AttributeValue>, core::StringViewLess>;

std::string encode_attributes(const RecordAttributes& attrs);

// Returns nullopt if the buffer is malformed or truncated.
std::optional<RecordAttributes> decode_attributes(std::string_view data);

}  // namespace cynamodb::engine::lsm
