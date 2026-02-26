#include <cynamodb/engine/item_validator.hpp>
#include <algorithm>
#include <limits>

namespace cynamodb::engine {

namespace {

size_t saturating_add(size_t a, size_t b) {
    if (std::numeric_limits<size_t>::max() - a < b) return std::numeric_limits<size_t>::max();
    return a + b;
}

size_t calculate_attr_size(const core::AttributeValue& val) {
    switch (val.type) {
        case core::AttributeType::S:
            return std::get<core::String>(val.value).size();
        case core::AttributeType::N:
            return std::get<core::String>(val.value).size();
        case core::AttributeType::B:
            return std::get<std::pmr::vector<uint8_t>>(val.value).size();
        case core::AttributeType::BOOL:
        case core::AttributeType::NUL:
            return 1;
        case core::AttributeType::M: {
            size_t s = 3; // Overhead
            for (const auto& [k, v] : std::get<core::MapValue>(val.value)) {
                s = saturating_add(s, k.size());
                s = saturating_add(s, calculate_attr_size(*v));
                s = saturating_add(s, 1); // overhead per entry
            }
            return s;
        }
        case core::AttributeType::L: {
            size_t s = 3;
            for (const auto& v : std::get<core::ListValue>(val.value)) {
                s = saturating_add(s, calculate_attr_size(*v));
                s = saturating_add(s, 1);
            }
            return s;
        }
        case core::AttributeType::SS:
        case core::AttributeType::NS: {
            size_t s = 3;
            const auto& vec = val.type == core::AttributeType::SS ? 
                std::get<core::StringSet>(val.value).values : std::get<core::NumberSet>(val.value).values;
            for (const auto& v : vec) {
                s = saturating_add(s, v.size());
            }
            return s;
        }
        case core::AttributeType::BS: {
            size_t s = 3;
            for (const auto& v : std::get<core::BinarySet>(val.value).values) {
                s = saturating_add(s, v.size());
            }
            return s;
        }
    }
    return 0;
}

} // namespace

size_t ItemValidator::calculate_item_size(const StorageEngine::AttributeMap& item) {
    size_t size = 0;
    for (const auto& [k, v] : item) {
        size = saturating_add(size, k.size());
        if (v) size = saturating_add(size, calculate_attr_size(*v));
    }
    return size;
}

std::expected<void, ValidationError> ItemValidator::validate_attribute(
    std::string_view name,
    const std::shared_ptr<core::AttributeValue>& val,
    size_t depth) {
    
    if (depth > kMaxNestingDepth) return std::unexpected(ValidationError::NestingDepthExceeded);
    if (name.size() > kMaxAttributeNameBytes) return std::unexpected(ValidationError::InvalidAttributeName);
    if (!val) return {};

    if (val->type == core::AttributeType::M) {
        for (const auto& [k, v] : std::get<core::MapValue>(val->value)) {
            auto res = validate_attribute(k, v, depth + 1);
            if (!res) return res;
        }
    } else if (val->type == core::AttributeType::L) {
        for (const auto& v : std::get<core::ListValue>(val->value)) {
            auto res = validate_attribute("", v, depth + 1); // List elements have no name
            if (!res) return res;
        }
    }

    return {};
}

std::expected<void, ValidationError> ItemValidator::validate_item_standard(
    const StorageEngine::AttributeMap& item,
    const core::TableDefinition& table_def) {
    
    if (calculate_item_size(item) > kMaxItemSizeBytes) {
        return std::unexpected(ValidationError::ItemTooLarge);
    }

    for (const auto& [name, val] : item) {
        auto res = validate_attribute(name, val, 1);
        if (!res) return res;
    }

    // Key Type Validation
    for (const auto& ks : table_def.key_schema) {
        auto it = item.find(ks.attribute_name);
        if (it == item.end()) return std::unexpected(ValidationError::TypeMismatchForKey); // Missing key
        
        auto def_it = table_def.attribute_definitions.find(ks.attribute_name);
        if (def_it != table_def.attribute_definitions.end()) {
            if (it->second->type != def_it->second) {
                return std::unexpected(ValidationError::TypeMismatchForKey);
            }
        }
    }

    return {};
}

} // namespace cynamodb::engine
