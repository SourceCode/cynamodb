#include <cynamodb/engine/item_validator.hpp>
#include <cynamodb/core/sizing.hpp>
#include <algorithm>
#include <cctype>
#include <limits>
#include <set>

namespace cynamodb::engine {

namespace {

// Validates a string against DynamoDB's number grammar: optional sign, an integer
// and/or fraction part with at least one digit, an optional exponent, and at most
// 38 significant digits. Used to reject garbage stored as N/NS (which would also
// corrupt the order-preserving numeric key codec).
bool is_valid_number(std::string_view s) {
    size_t i = 0;
    const size_t n = s.size();
    if (n == 0) return false;
    if (s[i] == '+' || s[i] == '-') ++i;

    size_t significant = 0;
    bool any_digit = false;
    bool seen_nonzero = false;
    auto count_digit = [&](char c) {
        any_digit = true;
        if (c != '0' || seen_nonzero) { seen_nonzero = true; ++significant; }
    };

    while (i < n && std::isdigit(static_cast<unsigned char>(s[i]))) count_digit(s[i++]);
    if (i < n && s[i] == '.') {
        ++i;
        while (i < n && std::isdigit(static_cast<unsigned char>(s[i]))) count_digit(s[i++]);
    }
    if (!any_digit) return false;

    if (i < n && (s[i] == 'e' || s[i] == 'E')) {
        ++i;
        if (i < n && (s[i] == '+' || s[i] == '-')) ++i;
        size_t exp_digits = 0;
        while (i < n && std::isdigit(static_cast<unsigned char>(s[i]))) { ++i; ++exp_digits; }
        if (exp_digits == 0) return false;
    }
    if (i != n) return false;             // trailing junk
    return significant <= 38;
}

} // namespace

size_t ItemValidator::calculate_item_size(const StorageEngine::AttributeMap& item) {
    // Sizing delegates to the shared core implementation (see core/sizing.hpp) so
    // the validator and the JSON serializer can never disagree.
    size_t size = 0;
    for (const auto& [k, v] : item) {
        size = core::size_saturating_add(size, k.size());
        if (v) size = core::size_saturating_add(size, core::attribute_size(*v));
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

    if (val->type == core::AttributeType::N) {
        if (!is_valid_number(std::get<core::String>(val->value))) {
            return std::unexpected(ValidationError::InvalidNumber);
        }
    } else if (val->type == core::AttributeType::NS) {
        const auto& vals = std::get<core::NumberSet>(val->value).values;
        if (vals.empty()) return std::unexpected(ValidationError::EmptySet);
        std::set<std::string_view> seen;
        for (const auto& v : vals) {
            if (!is_valid_number(v)) return std::unexpected(ValidationError::InvalidNumber);
            if (!seen.insert(std::string_view(v.data(), v.size())).second) {
                return std::unexpected(ValidationError::DuplicateSetValue);
            }
        }
    } else if (val->type == core::AttributeType::SS) {
        const auto& vals = std::get<core::StringSet>(val->value).values;
        if (vals.empty()) return std::unexpected(ValidationError::EmptySet);
        std::set<std::string_view> seen;
        for (const auto& v : vals) {
            if (!seen.insert(std::string_view(v.data(), v.size())).second) {
                return std::unexpected(ValidationError::DuplicateSetValue);
            }
        }
    } else if (val->type == core::AttributeType::BS) {
        const auto& vals = std::get<core::BinarySet>(val->value).values;
        if (vals.empty()) return std::unexpected(ValidationError::EmptySet);
        std::set<std::vector<uint8_t>> seen;
        for (const auto& v : vals) {
            if (!seen.insert(std::vector<uint8_t>(v.begin(), v.end())).second) {
                return std::unexpected(ValidationError::DuplicateSetValue);
            }
        }
    } else if (val->type == core::AttributeType::M) {
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
        if (it == item.end() || !it->second) return std::unexpected(ValidationError::TypeMismatchForKey); // Missing key

        auto def_it = table_def.attribute_definitions.find(ks.attribute_name);
        if (def_it != table_def.attribute_definitions.end()) {
            if (it->second->type != def_it->second) {
                return std::unexpected(ValidationError::TypeMismatchForKey);
            }
        }

        // AWS rejects empty String/Binary values in key attributes; accepting them
        // would mask client bugs that fail against real DynamoDB.
        if (it->second->type == core::AttributeType::S &&
            std::get<core::String>(it->second->value).empty()) {
            return std::unexpected(ValidationError::EmptyKeyAttribute);
        }
        if (it->second->type == core::AttributeType::B &&
            std::get<std::pmr::vector<uint8_t>>(it->second->value).empty()) {
            return std::unexpected(ValidationError::EmptyKeyAttribute);
        }
    }

    return {};
}

} // namespace cynamodb::engine
