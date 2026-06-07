#pragma once

#include <cynamodb/engine/storage_engine.hpp>
#include <cynamodb/core/schema.hpp>
#include <expected>
#include <vector>
#include <string>

namespace cynamodb::engine {

enum class ValidationError {
    ItemTooLarge,
    InvalidAttributeName,
    NestingDepthExceeded,
    TypeMismatchForKey,
    EmptyKeyAttribute,
    InvalidNumber,
    EmptySet,
    DuplicateSetValue,
    ProvisionedThroughputExceeded
};

class ItemValidator {
public:
    static constexpr size_t kMaxItemSizeBytes = 400000;
    static constexpr size_t kMaxAttributeNameBytes = 255;
    static constexpr size_t kMaxNestingDepth = 32;

    static std::expected<void, ValidationError> validate_item_standard(
        const StorageEngine::AttributeMap& item,
        const core::TableDefinition& table_def);

    static size_t calculate_item_size(const StorageEngine::AttributeMap& item);

private:
    static std::expected<void, ValidationError> validate_attribute(
        std::string_view name,
        const std::shared_ptr<core::AttributeValue>& val,
        size_t depth);
};

} // namespace cynamodb::engine
