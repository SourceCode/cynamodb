#include <cynamodb/api/dispatcher.hpp>
#include <array>
#include <algorithm>
#include <cctype>

namespace cynamodb::api {

namespace {

constexpr std::string_view kDynamoDbTargetPrefix = "DynamoDB_20120810.";
constexpr std::string_view kStreamsTargetPrefix = "DynamoDBStreams_20120810.";

bool has_invalid_target_chars(std::string_view target) {
    return std::any_of(target.begin(), target.end(), [](unsigned char c) {
        if (c < 0x21 || c > 0x7E) {
            return true;
        }
        return false;
    });
}

bool is_valid_operation_name(std::string_view op_name, size_t min_len, size_t max_len) {
    if (op_name.empty() || op_name.size() < min_len || op_name.size() > max_len) {
        return false;
    }
    if (std::isalpha(static_cast<unsigned char>(op_name.front())) == 0 ||
        std::isupper(static_cast<unsigned char>(op_name.front())) == 0) {
        return false;
    }
    if (std::isdigit(static_cast<unsigned char>(op_name.front())) != 0) {
        return false;
    }
    bool has_lower = false;
    for (unsigned char c : op_name) {
        if (std::isalnum(c) == 0) {
            return false;
        }
        if (std::islower(c) != 0) {
            has_lower = true;
        }
    }
    return has_lower;
}

[[maybe_unused]] bool is_streams_operation(Operation op) {
    return op == Operation::ListStreams ||
           op == Operation::DescribeStream ||
           op == Operation::GetShardIterator ||
           op == Operation::GetRecords;
}

template <size_t N>
constexpr bool is_sorted_lookup(const std::array<std::pair<std::string_view, Operation>, N>& values) {
    for (size_t i = 1; i < N; ++i) {
        if (!(values[i - 1].first < values[i].first)) {
            return false;
        }
    }
    return true;
}

} // namespace

Operation ApiDispatcher::parse_target(std::string_view target) {
    constexpr size_t kMaxTargetBytes = 256;
    constexpr size_t kMinTargetBytes = 20;
    constexpr size_t kMaxOperationBytes = 64;
    constexpr size_t kMinOperationBytes = 3;
    while (!target.empty() && std::isspace(static_cast<unsigned char>(target.front())) != 0) {
        target.remove_prefix(1);
    }
    while (!target.empty() && std::isspace(static_cast<unsigned char>(target.back())) != 0) {
        target.remove_suffix(1);
    }
    if (target.empty() || target.size() > kMaxTargetBytes || target.size() < kMinTargetBytes) {
        return Operation::Unknown;
    }
    if (has_invalid_target_chars(target)) {
        return Operation::Unknown;
    }

    std::string_view prefix;
    if (target.starts_with(kDynamoDbTargetPrefix)) {
        prefix = kDynamoDbTargetPrefix;
    } else if (target.starts_with(kStreamsTargetPrefix)) {
        prefix = kStreamsTargetPrefix;
    } else {
        return Operation::Unknown;
    }

    const auto op_name = target.substr(prefix.size());
    if (!is_valid_operation_name(op_name, kMinOperationBytes, kMaxOperationBytes)) {
        return Operation::Unknown;
    }
    if (op_name.find('.') != std::string_view::npos) {
        return Operation::Unknown;
    }

    static constexpr std::array<std::pair<std::string_view, Operation>, 61> kLookup{{
        {"BatchExecuteStatement", Operation::BatchExecuteStatement},
        {"BatchGetItem", Operation::BatchGetItem},
        {"BatchWriteItem", Operation::BatchWriteItem},
        {"CreateBackup", Operation::CreateBackup},
        {"CreateGlobalTable", Operation::CreateGlobalTable},
        {"CreateTable", Operation::CreateTable},
        {"DeleteBackup", Operation::DeleteBackup},
        {"DeleteItem", Operation::DeleteItem},
        {"DeleteResourcePolicy", Operation::DeleteResourcePolicy},
        {"DeleteTable", Operation::DeleteTable},
        {"DescribeBackup", Operation::DescribeBackup},
        {"DescribeContinuousBackups", Operation::DescribeContinuousBackups},
        {"DescribeContributorInsights", Operation::DescribeContributorInsights},
        {"DescribeEndpoints", Operation::DescribeEndpoints},
        {"DescribeExport", Operation::DescribeExport},
        {"DescribeGlobalTable", Operation::DescribeGlobalTable},
        {"DescribeGlobalTableSettings", Operation::DescribeGlobalTableSettings},
        {"DescribeImport", Operation::DescribeImport},
        {"DescribeKinesisStreamingDestination", Operation::DescribeKinesisStreamingDestination},
        {"DescribeLimits", Operation::DescribeLimits},
        {"DescribeStream", Operation::DescribeStream},
        {"DescribeTable", Operation::DescribeTable},
        {"DescribeTableReplicaAutoScaling", Operation::DescribeTableReplicaAutoScaling},
        {"DescribeTimeToLive", Operation::DescribeTimeToLive},
        {"DisableKinesisStreamingDestination", Operation::DisableKinesisStreamingDestination},
        {"EnableKinesisStreamingDestination", Operation::EnableKinesisStreamingDestination},
        {"ExecuteStatement", Operation::ExecuteStatement},
        {"ExecuteTransaction", Operation::ExecuteTransaction},
        {"ExportTableToPointInTime", Operation::ExportTableToPointInTime},
        {"GetItem", Operation::GetItem},
        {"GetRecords", Operation::GetRecords},
        {"GetResourcePolicy", Operation::GetResourcePolicy},
        {"GetShardIterator", Operation::GetShardIterator},
        {"ImportTable", Operation::ImportTable},
        {"ListBackups", Operation::ListBackups},
        {"ListContributorInsights", Operation::ListContributorInsights},
        {"ListExports", Operation::ListExports},
        {"ListGlobalTables", Operation::ListGlobalTables},
        {"ListImports", Operation::ListImports},
        {"ListStreams", Operation::ListStreams},
        {"ListTables", Operation::ListTables},
        {"ListTagsOfResource", Operation::ListTagsOfResource},
        {"PutItem", Operation::PutItem},
        {"PutResourcePolicy", Operation::PutResourcePolicy},
        {"Query", Operation::Query},
        {"RestoreTableFromBackup", Operation::RestoreTableFromBackup},
        {"RestoreTableToPointInTime", Operation::RestoreTableToPointInTime},
        {"Scan", Operation::Scan},
        {"TagResource", Operation::TagResource},
        {"TransactGetItems", Operation::TransactGetItems},
        {"TransactWriteItems", Operation::TransactWriteItems},
        {"UntagResource", Operation::UntagResource},
        {"UpdateContinuousBackups", Operation::UpdateContinuousBackups},
        {"UpdateContributorInsights", Operation::UpdateContributorInsights},
        {"UpdateGlobalTable", Operation::UpdateGlobalTable},
        {"UpdateGlobalTableSettings", Operation::UpdateGlobalTableSettings},
        {"UpdateItem", Operation::UpdateItem},
        {"UpdateKinesisStreamingDestination", Operation::UpdateKinesisStreamingDestination},
        {"UpdateTable", Operation::UpdateTable},
        {"UpdateTableReplicaAutoScaling", Operation::UpdateTableReplicaAutoScaling},
        {"UpdateTimeToLive", Operation::UpdateTimeToLive}
    }};

    static_assert(is_sorted_lookup(kLookup), "kLookup must be sorted");

    const auto it = std::lower_bound(kLookup.begin(), kLookup.end(), op_name,
        [](const auto& pair, std::string_view val) {
            return pair.first < val;
        });

    if (it != kLookup.end() && it->first == op_name) {
        return it->second;
    }

    return Operation::Unknown;
}

std::string_view ApiDispatcher::to_string(Operation op) {
    switch (op) {
        case Operation::BatchExecuteStatement: return "BatchExecuteStatement";
        case Operation::BatchGetItem: return "BatchGetItem";
        case Operation::BatchWriteItem: return "BatchWriteItem";
        case Operation::CreateBackup: return "CreateBackup";
        case Operation::CreateGlobalTable: return "CreateGlobalTable";
        case Operation::CreateTable: return "CreateTable";
        case Operation::DeleteBackup: return "DeleteBackup";
        case Operation::DeleteItem: return "DeleteItem";
        case Operation::DeleteResourcePolicy: return "DeleteResourcePolicy";
        case Operation::DeleteTable: return "DeleteTable";
        case Operation::DescribeBackup: return "DescribeBackup";
        case Operation::DescribeContinuousBackups: return "DescribeContinuousBackups";
        case Operation::DescribeContributorInsights: return "DescribeContributorInsights";
        case Operation::DescribeEndpoints: return "DescribeEndpoints";
        case Operation::DescribeExport: return "DescribeExport";
        case Operation::DescribeGlobalTable: return "DescribeGlobalTable";
        case Operation::DescribeGlobalTableSettings: return "DescribeGlobalTableSettings";
        case Operation::DescribeImport: return "DescribeImport";
        case Operation::DescribeKinesisStreamingDestination: return "DescribeKinesisStreamingDestination";
        case Operation::DescribeLimits: return "DescribeLimits";
        case Operation::DescribeStream: return "DescribeStream";
        case Operation::DescribeTable: return "DescribeTable";
        case Operation::DescribeTableReplicaAutoScaling: return "DescribeTableReplicaAutoScaling";
        case Operation::DescribeTimeToLive: return "DescribeTimeToLive";
        case Operation::DisableKinesisStreamingDestination: return "DisableKinesisStreamingDestination";
        case Operation::EnableKinesisStreamingDestination: return "EnableKinesisStreamingDestination";
        case Operation::ExecuteStatement: return "ExecuteStatement";
        case Operation::ExecuteTransaction: return "ExecuteTransaction";
        case Operation::ExportTableToPointInTime: return "ExportTableToPointInTime";
        case Operation::GetItem: return "GetItem";
        case Operation::GetRecords: return "GetRecords";
        case Operation::GetResourcePolicy: return "GetResourcePolicy";
        case Operation::GetShardIterator: return "GetShardIterator";
        case Operation::ImportTable: return "ImportTable";
        case Operation::ListBackups: return "ListBackups";
        case Operation::ListContributorInsights: return "ListContributorInsights";
        case Operation::ListExports: return "ListExports";
        case Operation::ListGlobalTables: return "ListGlobalTables";
        case Operation::ListImports: return "ListImports";
        case Operation::ListStreams: return "ListStreams";
        case Operation::ListTables: return "ListTables";
        case Operation::ListTagsOfResource: return "ListTagsOfResource";
        case Operation::PutItem: return "PutItem";
        case Operation::PutResourcePolicy: return "PutResourcePolicy";
        case Operation::Query: return "Query";
        case Operation::RestoreTableFromBackup: return "RestoreTableFromBackup";
        case Operation::RestoreTableToPointInTime: return "RestoreTableToPointInTime";
        case Operation::Scan: return "Scan";
        case Operation::TagResource: return "TagResource";
        case Operation::TransactGetItems: return "TransactGetItems";
        case Operation::TransactWriteItems: return "TransactWriteItems";
        case Operation::UntagResource: return "UntagResource";
        case Operation::UpdateContinuousBackups: return "UpdateContinuousBackups";
        case Operation::UpdateContributorInsights: return "UpdateContributorInsights";
        case Operation::UpdateGlobalTable: return "UpdateGlobalTable";
        case Operation::UpdateGlobalTableSettings: return "UpdateGlobalTableSettings";
        case Operation::UpdateItem: return "UpdateItem";
        case Operation::UpdateKinesisStreamingDestination: return "UpdateKinesisStreamingDestination";
        case Operation::UpdateTable: return "UpdateTable";
        case Operation::UpdateTableReplicaAutoScaling: return "UpdateTableReplicaAutoScaling";
        case Operation::UpdateTimeToLive: return "UpdateTimeToLive";
        case Operation::Unknown: return "Unknown";
    }
    return "Unknown";
}

} // namespace cynamodb::api
