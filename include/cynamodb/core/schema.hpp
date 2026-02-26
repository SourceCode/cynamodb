#pragma once

#include <string>
#include <vector>
#include <map>
#include <cstdint>
#include <optional>
#include <cynamodb/core/types.hpp>

namespace cynamodb::core {

enum class KeyType {
    HASH,
    RANGE
};

struct KeySchemaElement {
    std::string attribute_name;
    KeyType key_type;
};

enum class ProjectionType {
    KEYS_ONLY,
    INCLUDE,
    ALL
};

struct Projection {
    ProjectionType projection_type;
    std::vector<std::string> non_key_attributes;
};

struct ProvisionedThroughput {
    uint64_t read_capacity_units = 0;
    uint64_t write_capacity_units = 0;
};

enum class BillingMode {
    PROVISIONED,
    PAY_PER_REQUEST
};

enum class StreamViewType {
    KEYS_ONLY,
    NEW_IMAGE,
    OLD_IMAGE,
    NEW_AND_OLD_IMAGES
};

struct StreamSpecification {
    bool stream_enabled = false;
    std::optional<StreamViewType> stream_view_type;
};

enum class TableClass {
    STANDARD,
    STANDARD_INFREQUENT_ACCESS
};

struct LocalSecondaryIndex {
    std::string index_name;
    std::vector<KeySchemaElement> key_schema;
    Projection projection;
};

struct GlobalSecondaryIndex {
    std::string index_name;
    std::vector<KeySchemaElement> key_schema;
    Projection projection;
    ProvisionedThroughput provisioned_throughput;
};

struct TimeToLiveSpecification {
    std::string attribute_name;
    bool enabled = false;
};

struct PointInTimeRecoverySpecification {
    bool point_in_time_recovery_enabled = false;
    uint32_t recovery_period_in_days = 35;
    uint64_t point_in_time_recovery_enabled_epoch_seconds = 0;
};

struct TableDefinition {
    std::string table_name;
    std::vector<KeySchemaElement> key_schema;
    std::map<std::string, AttributeType, StringViewLess> attribute_definitions;
    BillingMode billing_mode = BillingMode::PAY_PER_REQUEST;
    ProvisionedThroughput provisioned_throughput;
    TableClass table_class = TableClass::STANDARD;
    bool deletion_protection_enabled = false;
    uint64_t creation_epoch_seconds = 0;
    std::map<std::string, std::string, StringViewLess> tags;
    std::vector<LocalSecondaryIndex> local_secondary_indexes;
    std::vector<GlobalSecondaryIndex> global_secondary_indexes;
    std::optional<StreamSpecification> stream_specification;
    std::optional<std::string> latest_stream_arn;
    std::optional<std::string> latest_stream_label;
    std::optional<TimeToLiveSpecification> ttl_specification;
    PointInTimeRecoverySpecification point_in_time_recovery;
};

} // namespace cynamodb::core
