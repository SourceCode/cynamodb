#pragma once

// Single source of truth for DynamoDB attribute-value sizing. Previously the item
// validator and the JSON serializer each carried their own (divergent)
// `calculate_attr_size` — the serializer's returned 0 for every non-scalar type.
// Both now delegate here so size accounting can never disagree.

#include <cstddef>
#include <limits>
#include <variant>

#include <cynamodb/core/types.hpp>

namespace cynamodb::core {

inline size_t size_saturating_add(size_t a, size_t b) {
    if (std::numeric_limits<size_t>::max() - a < b) return std::numeric_limits<size_t>::max();
    return a + b;
}

inline size_t attribute_size(const AttributeValue& val) {
    switch (val.type) {
        case AttributeType::S:
        case AttributeType::N:
            return std::get<String>(val.value).size();
        case AttributeType::B:
            return std::get<std::pmr::vector<uint8_t>>(val.value).size();
        case AttributeType::BOOL:
        case AttributeType::NUL:
            return 1;
        case AttributeType::M: {
            size_t s = 3;  // structural overhead
            for (const auto& [k, v] : std::get<MapValue>(val.value)) {
                s = size_saturating_add(s, k.size());
                if (v) s = size_saturating_add(s, attribute_size(*v));
                s = size_saturating_add(s, 1);
            }
            return s;
        }
        case AttributeType::L: {
            size_t s = 3;
            for (const auto& v : std::get<ListValue>(val.value)) {
                if (v) s = size_saturating_add(s, attribute_size(*v));
                s = size_saturating_add(s, 1);
            }
            return s;
        }
        case AttributeType::SS: {
            size_t s = 3;
            for (const auto& v : std::get<StringSet>(val.value).values) s = size_saturating_add(s, v.size());
            return s;
        }
        case AttributeType::NS: {
            size_t s = 3;
            for (const auto& v : std::get<NumberSet>(val.value).values) s = size_saturating_add(s, v.size());
            return s;
        }
        case AttributeType::BS: {
            size_t s = 3;
            for (const auto& v : std::get<BinarySet>(val.value).values) s = size_saturating_add(s, v.size());
            return s;
        }
    }
    return 0;
}

}  // namespace cynamodb::core
