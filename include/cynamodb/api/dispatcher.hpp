#pragma once

#include <string>
#include <string_view>
#include <optional>
#include <map>
#include <functional>

namespace cynamodb::api {

enum class Operation {
    BatchExecuteStatement,
    BatchGetItem,
    BatchWriteItem,
    CreateBackup,
    CreateGlobalTable,
    CreateTable,
    DeleteBackup,
    DeleteItem,
    DeleteResourcePolicy,
    DeleteTable,
    DescribeBackup,
    DescribeContinuousBackups,
    DescribeContributorInsights,
    DescribeEndpoints,
    DescribeExport,
    DescribeGlobalTable,
    DescribeGlobalTableSettings,
    DescribeImport,
    DescribeKinesisStreamingDestination,
    DescribeLimits,
    DescribeTable,
    DescribeTableReplicaAutoScaling,
    DescribeTimeToLive,
    DisableKinesisStreamingDestination,
    EnableKinesisStreamingDestination,
    ExecuteStatement,
    ExecuteTransaction,
    ExportTableToPointInTime,
    GetItem,
    GetResourcePolicy,
    ImportTable,
    ListBackups,
    ListContributorInsights,
    ListExports,
    ListGlobalTables,
    ListImports,
    ListTables,
    ListTagsOfResource,
    PutItem,
    PutResourcePolicy,
    Query,
    RestoreTableFromBackup,
    RestoreTableToPointInTime,
    Scan,
    TagResource,
    TransactGetItems,
    TransactWriteItems,
    UntagResource,
    UpdateContinuousBackups,
    UpdateContributorInsights,
    UpdateGlobalTable,
    UpdateGlobalTableSettings,
    UpdateItem,
    UpdateKinesisStreamingDestination,
    UpdateTable,
    UpdateTableReplicaAutoScaling,
    UpdateTimeToLive,
    ListStreams,
    DescribeStream,
    GetShardIterator,
    GetRecords,
    Unknown
};

class ApiDispatcher {
public:
    static Operation parse_target(std::string_view target);
    static std::string_view to_string(Operation op);
};

} // namespace cynamodb::api
