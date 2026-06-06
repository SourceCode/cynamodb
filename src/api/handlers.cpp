#include <cynamodb/api/handlers.hpp>

#include <cynamodb/engine/item_validator.hpp>
#include <cynamodb/engine/key_codec.hpp>
#include <cynamodb/json/serializer.hpp>

#include <simdjson.h>

#include <optional>
#include <string>
#include <vector>

namespace cynamodb::api {

namespace {

using AttributeMap = engine::StorageEngine::AttributeMap;

ApiResult ok(std::string body) {
    return ApiResult{200, "", std::move(body)};
}

ApiResult error(unsigned status, const std::string& type, const std::string& message) {
    return ApiResult{status, type,
                     json::JsonSerializer::serialize_error(
                         "com.amazonaws.dynamodb.v20120810#" + type, message)};
}

std::string key_type_str(core::KeyType t) {
    return t == core::KeyType::HASH ? "HASH" : "RANGE";
}

std::string attribute_type_str(core::AttributeType t) {
    switch (t) {
        case core::AttributeType::S: return "S";
        case core::AttributeType::N: return "N";
        case core::AttributeType::B: return "B";
        default: return "S";
    }
}

// Parses a DynamoDB attribute-map element ({"attr": {"S": "v"}, ...}). Returns
// nullopt on malformed input.
std::optional<AttributeMap> parse_attribute_map(simdjson::dom::element el) {
    simdjson::dom::object obj;
    if (el.get_object().get(obj) != simdjson::SUCCESS) {
        return std::nullopt;
    }
    AttributeMap map;
    try {
        for (auto field : obj) {
            map[std::string(field.key)] =
                std::make_shared<core::AttributeValue>(json::JsonParser::parse_attribute_value(field.value));
        }
    } catch (const std::exception&) {
        return std::nullopt;
    }
    return map;
}

// Serializes a DynamoDB TableDescription-style object body for a table.
std::string serialize_table_description(const core::TableDefinition& def) {
    std::string out = "{\"TableName\":\"" + def.table_name + "\",\"TableStatus\":\"ACTIVE\"";

    out += ",\"KeySchema\":[";
    for (size_t i = 0; i < def.key_schema.size(); ++i) {
        if (i) out += ",";
        out += "{\"AttributeName\":\"" + def.key_schema[i].attribute_name +
               "\",\"KeyType\":\"" + key_type_str(def.key_schema[i].key_type) + "\"}";
    }
    out += "]";

    out += ",\"AttributeDefinitions\":[";
    bool first = true;
    for (const auto& [name, type] : def.attribute_definitions) {
        if (!first) out += ",";
        out += "{\"AttributeName\":\"" + name + "\",\"AttributeType\":\"" + attribute_type_str(type) + "\"}";
        first = false;
    }
    out += "]";

    out += ",\"ItemCount\":0";
    out += ",\"TableArn\":\"arn:aws:dynamodb:ddblocal:000000000000:table/" + def.table_name + "\"";
    out += "}";
    return out;
}

// Builds a JSON object containing only a table's key attributes, drawn from a
// full item. Used to emit LastEvaluatedKey for pagination.
std::string key_only_json(const core::TableDefinition& def, const AttributeMap& item) {
    AttributeMap key;
    for (const auto& ks : def.key_schema) {
        auto it = item.find(ks.attribute_name);
        if (it != item.end()) {
            key[it->first] = it->second;
        }
    }
    return json::JsonSerializer::serialize_item(key);
}

bool item_has_all_keys(const core::TableDefinition& def, const AttributeMap& item) {
    for (const auto& ks : def.key_schema) {
        auto it = item.find(ks.attribute_name);
        if (it == item.end() || !it->second) return false;
    }
    return true;
}

std::optional<size_t> read_limit(simdjson::dom::element doc) {
    int64_t limit = 0;
    if (doc["Limit"].get_int64().get(limit) == simdjson::SUCCESS && limit > 0) {
        return static_cast<size_t>(limit);
    }
    return std::nullopt;
}

// Returns the encoded exclusive-start-key token from the request, if present.
std::optional<std::string> read_start_key(simdjson::dom::element doc, const core::TableDefinition& def) {
    simdjson::dom::element esk;
    if (doc["ExclusiveStartKey"].get(esk) != simdjson::SUCCESS) {
        return std::nullopt;
    }
    auto map = parse_attribute_map(esk);
    if (!map) return std::nullopt;
    return engine::encode_primary_key(def, *map);
}

std::string items_to_json(const std::vector<AttributeMap>& items) {
    std::string out = "[";
    for (size_t i = 0; i < items.size(); ++i) {
        if (i) out += ",";
        out += json::JsonSerializer::serialize_item(items[i]);
    }
    out += "]";
    return out;
}

// ---- Operation handlers -------------------------------------------------

ApiResult handle_create_table(engine::TableManager& tables, simdjson::dom::element doc) {
    core::TableDefinition def = json::JsonParser::parse_table_definition(doc);
    if (def.table_name.empty()) {
        return error(400, "ValidationException", "TableName is required");
    }
    if (def.key_schema.empty()) {
        return error(400, "ValidationException", "KeySchema is required");
    }
    auto created = tables.create_table(def);
    if (!created) {
        if (created.error() == engine::TableError::TableAlreadyExists) {
            return error(400, "ResourceInUseException",
                         "Table already exists: " + def.table_name);
        }
        return error(500, "InternalServerError", "Failed to create table");
    }
    return ok("{\"TableDescription\":" + serialize_table_description(*created) + "}");
}

ApiResult handle_describe_table(engine::TableManager& tables, simdjson::dom::element doc) {
    std::string_view name;
    if (doc["TableName"].get_string().get(name) != simdjson::SUCCESS) {
        return error(400, "ValidationException", "TableName is required");
    }
    auto def = tables.describe_table(name);
    if (!def) {
        return error(400, "ResourceNotFoundException",
                     "Requested resource not found: Table: " + std::string(name) + " not found");
    }
    return ok("{\"Table\":" + serialize_table_description(*def) + "}");
}

ApiResult handle_list_tables(engine::TableManager& tables) {
    auto names = tables.list_tables();
    std::string out = "{\"TableNames\":[";
    for (size_t i = 0; i < names.size(); ++i) {
        if (i) out += ",";
        out += "\"" + names[i] + "\"";
    }
    out += "]}";
    return ok(std::move(out));
}

ApiResult handle_put_item(engine::TableManager& tables, engine::StorageEngine& storage,
                          simdjson::dom::element doc) {
    std::string_view name;
    if (doc["TableName"].get_string().get(name) != simdjson::SUCCESS) {
        return error(400, "ValidationException", "TableName is required");
    }
    auto def = tables.describe_table(name);
    if (!def) {
        return error(400, "ResourceNotFoundException",
                     "Requested resource not found: Table: " + std::string(name) + " not found");
    }

    simdjson::dom::element item_el;
    if (doc["Item"].get(item_el) != simdjson::SUCCESS) {
        return error(400, "ValidationException", "Item is required");
    }
    auto item = parse_attribute_map(item_el);
    if (!item) {
        return error(400, "ValidationException", "Item is not a valid attribute map");
    }
    if (!item_has_all_keys(*def, *item)) {
        return error(400, "ValidationException",
                     "One of the required keys was not given a value");
    }

    auto valid = engine::ItemValidator::validate_item_standard(*item, *def);
    if (!valid) {
        if (valid.error() == engine::ValidationError::ItemTooLarge) {
            return error(400, "ValidationException", "Item size has exceeded the maximum allowed size");
        }
        if (valid.error() == engine::ValidationError::TypeMismatchForKey) {
            return error(400, "ValidationException",
                         "Type mismatch for key attribute against the table schema");
        }
        return error(400, "ValidationException", "Item failed validation");
    }

    std::string key = engine::encode_primary_key(*def, *item);
    storage.put(std::string(name), key, *item);
    return ok("{}");
}

ApiResult handle_get_item(engine::TableManager& tables, engine::StorageEngine& storage,
                          simdjson::dom::element doc) {
    std::string_view name;
    if (doc["TableName"].get_string().get(name) != simdjson::SUCCESS) {
        return error(400, "ValidationException", "TableName is required");
    }
    auto def = tables.describe_table(name);
    if (!def) {
        return error(400, "ResourceNotFoundException",
                     "Requested resource not found: Table: " + std::string(name) + " not found");
    }
    simdjson::dom::element key_el;
    if (doc["Key"].get(key_el) != simdjson::SUCCESS) {
        return error(400, "ValidationException", "Key is required");
    }
    auto key_map = parse_attribute_map(key_el);
    if (!key_map || !item_has_all_keys(*def, *key_map)) {
        return error(400, "ValidationException", "The provided key element does not match the schema");
    }

    std::string key = engine::encode_primary_key(*def, *key_map);
    auto item = storage.get(std::string(name), key);
    if (!item) {
        return ok("{}");
    }
    return ok("{\"Item\":" + json::JsonSerializer::serialize_item(*item) + "}");
}

ApiResult handle_delete_item(engine::TableManager& tables, engine::StorageEngine& storage,
                             simdjson::dom::element doc) {
    std::string_view name;
    if (doc["TableName"].get_string().get(name) != simdjson::SUCCESS) {
        return error(400, "ValidationException", "TableName is required");
    }
    auto def = tables.describe_table(name);
    if (!def) {
        return error(400, "ResourceNotFoundException",
                     "Requested resource not found: Table: " + std::string(name) + " not found");
    }
    simdjson::dom::element key_el;
    if (doc["Key"].get(key_el) != simdjson::SUCCESS) {
        return error(400, "ValidationException", "Key is required");
    }
    auto key_map = parse_attribute_map(key_el);
    if (!key_map || !item_has_all_keys(*def, *key_map)) {
        return error(400, "ValidationException", "The provided key element does not match the schema");
    }
    std::string key = engine::encode_primary_key(*def, *key_map);
    storage.remove(std::string(name), key);
    return ok("{}");
}

ApiResult handle_scan(engine::TableManager& tables, engine::StorageEngine& storage,
                      simdjson::dom::element doc) {
    std::string_view name;
    if (doc["TableName"].get_string().get(name) != simdjson::SUCCESS) {
        return error(400, "ValidationException", "TableName is required");
    }
    auto def = tables.describe_table(name);
    if (!def) {
        return error(400, "ResourceNotFoundException",
                     "Requested resource not found: Table: " + std::string(name) + " not found");
    }

    size_t limit = read_limit(doc).value_or(0);
    auto start = read_start_key(doc, *def);
    auto result = storage.scan(std::string(name), start, limit);

    std::string out = "{\"Items\":" + items_to_json(result.items) +
                      ",\"Count\":" + std::to_string(result.items.size()) +
                      ",\"ScannedCount\":" + std::to_string(result.items.size());
    if (result.last_evaluated_key && !result.items.empty()) {
        out += ",\"LastEvaluatedKey\":" + key_only_json(*def, result.items.back());
    }
    out += "}";
    return ok(std::move(out));
}

ApiResult handle_query(engine::TableManager& tables, engine::StorageEngine& storage,
                       simdjson::dom::element doc) {
    std::string_view name;
    if (doc["TableName"].get_string().get(name) != simdjson::SUCCESS) {
        return error(400, "ValidationException", "TableName is required");
    }
    auto def = tables.describe_table(name);
    if (!def) {
        return error(400, "ResourceNotFoundException",
                     "Requested resource not found: Table: " + std::string(name) + " not found");
    }

    // Supported query form: legacy KeyConditions with EQ comparison operators.
    simdjson::dom::object conditions_obj;
    if (doc["KeyConditions"].get_object().get(conditions_obj) != simdjson::SUCCESS) {
        return error(400, "ValidationException",
                     "Query requires KeyConditions (KeyConditionExpression is not yet supported)");
    }

    AttributeMap conditions;
    for (auto field : conditions_obj) {
        std::string_view op;
        if (field.value["ComparisonOperator"].get_string().get(op) != simdjson::SUCCESS || op != "EQ") {
            return error(400, "ValidationException",
                         "Only the EQ comparison operator is supported in KeyConditions");
        }
        simdjson::dom::array values;
        if (field.value["AttributeValueList"].get_array().get(values) != simdjson::SUCCESS) {
            return error(400, "ValidationException", "AttributeValueList is required for each condition");
        }
        auto it = values.begin();
        if (it == values.end()) {
            return error(400, "ValidationException", "EQ requires exactly one attribute value");
        }
        try {
            conditions[std::string(field.key)] =
                std::make_shared<core::AttributeValue>(json::JsonParser::parse_attribute_value(*it));
        } catch (const std::exception&) {
            return error(400, "ValidationException", "Invalid attribute value in KeyConditions");
        }
    }
    if (conditions.empty()) {
        return error(400, "ValidationException", "KeyConditions must not be empty");
    }

    size_t limit = read_limit(doc).value_or(0);
    auto start = read_start_key(doc, *def);
    auto result = storage.query(std::string(name), conditions, start, limit);

    std::string out = "{\"Items\":" + items_to_json(result.items) +
                      ",\"Count\":" + std::to_string(result.items.size()) +
                      ",\"ScannedCount\":" + std::to_string(result.items.size());
    if (result.last_evaluated_key && !result.items.empty()) {
        out += ",\"LastEvaluatedKey\":" + key_only_json(*def, result.items.back());
    }
    out += "}";
    return ok(std::move(out));
}

}  // namespace

ApiResult handle_operation(engine::TableManager& tables, engine::StorageEngine& storage,
                           Operation op, std::string_view body) {
    simdjson::dom::parser parser;
    // An empty body is valid for some operations (e.g. ListTables); default to {}.
    std::string_view effective_body = body.empty() ? std::string_view("{}") : body;
    simdjson::padded_string json(effective_body);
    simdjson::dom::element doc;
    if (parser.parse(json).get(doc) != simdjson::SUCCESS) {
        return error(400, "SerializationException", "Could not parse the request body as JSON");
    }

    switch (op) {
        case Operation::CreateTable:   return handle_create_table(tables, doc);
        case Operation::DescribeTable: return handle_describe_table(tables, doc);
        case Operation::ListTables:    return handle_list_tables(tables);
        case Operation::PutItem:       return handle_put_item(tables, storage, doc);
        case Operation::GetItem:       return handle_get_item(tables, storage, doc);
        case Operation::DeleteItem:    return handle_delete_item(tables, storage, doc);
        case Operation::Scan:          return handle_scan(tables, storage, doc);
        case Operation::Query:         return handle_query(tables, storage, doc);
        default:
            return error(400, "UnknownOperationException",
                         "Operation is not supported by cynamoDB: " +
                             std::string(ApiDispatcher::to_string(op)));
    }
}

}  // namespace cynamodb::api
