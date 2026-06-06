#include <cynamodb/api/handlers.hpp>

#include <cynamodb/engine/item_validator.hpp>
#include <cynamodb/engine/key_codec.hpp>
#include <cynamodb/expressions/evaluator.hpp>
#include <cynamodb/expressions/lexer.hpp>
#include <cynamodb/expressions/parser.hpp>
#include <cynamodb/expressions/update_expression.hpp>
#include <cynamodb/json/serializer.hpp>

#include <simdjson.h>

#include <algorithm>
#include <cstdlib>
#include <optional>
#include <string>
#include <vector>

namespace cynamodb::api {

namespace {

using AttributeMap = engine::StorageEngine::AttributeMap;
using NameMap = std::map<std::string, std::string, core::StringViewLess>;
using Mutation = engine::StorageEngine::Mutation;
using MutationKind = engine::StorageEngine::MutationKind;

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

// Reads ExpressionAttributeNames into a placeholder->name map.
NameMap parse_expr_names(simdjson::dom::element doc) {
    NameMap names;
    simdjson::dom::object obj;
    if (doc["ExpressionAttributeNames"].get_object().get(obj) == simdjson::SUCCESS) {
        for (auto field : obj) {
            std::string_view v;
            if (field.value.get_string().get(v) == simdjson::SUCCESS) {
                names[std::string(field.key)] = std::string(v);
            }
        }
    }
    return names;
}

// Reads ExpressionAttributeValues into a placeholder->value map. Returns nullopt
// only if a value element is malformed.
std::optional<AttributeMap> parse_expr_values(simdjson::dom::element doc) {
    AttributeMap values;
    simdjson::dom::object obj;
    if (doc["ExpressionAttributeValues"].get_object().get(obj) != simdjson::SUCCESS) {
        return values;  // absent is fine
    }
    try {
        for (auto field : obj) {
            values[std::string(field.key)] =
                std::make_shared<core::AttributeValue>(json::JsonParser::parse_attribute_value(field.value));
        }
    } catch (const std::exception&) {
        return std::nullopt;
    }
    return values;
}

// Evaluates a ConditionExpression / FilterExpression against an item.
// Returns: 1 = passed, 0 = failed, -1 = parse error.
int evaluate_boolean_expr(const std::string& expr, const AttributeMap& item,
                          const NameMap& names, const AttributeMap& values) {
    expressions::Lexer lexer(expr);
    expressions::Parser parser(lexer.tokenize());
    auto ast = parser.parse_expression();
    if (!ast || !*ast) return -1;
    return expressions::Evaluator::evaluate_condition(**ast, item, names, values) ? 1 : 0;
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

// AWS rejects empty String/Binary values in key attributes. Returns false if a
// key attribute is present but empty.
bool key_values_non_empty(const core::TableDefinition& def, const AttributeMap& item) {
    for (const auto& ks : def.key_schema) {
        auto it = item.find(ks.attribute_name);
        if (it == item.end() || !it->second) continue;
        if (it->second->type == core::AttributeType::S &&
            std::get<core::String>(it->second->value).empty()) {
            return false;
        }
        if (it->second->type == core::AttributeType::B &&
            std::get<std::pmr::vector<uint8_t>>(it->second->value).empty()) {
            return false;
        }
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

// Merges a resolved value into the projected output along a document path,
// creating intermediate maps/lists. List indexes are compacted (AWS returns
// projected list elements positionally compacted), so each Index step appends.
void project_merge(std::shared_ptr<core::AttributeValue>& slot,
                   const std::vector<expressions::PathSegment>& segs, size_t i,
                   const NameMap& names, const std::shared_ptr<core::AttributeValue>& value) {
    if (i == segs.size()) { slot = value; return; }
    const auto& seg = segs[i];
    if (seg.kind == expressions::PathSegment::Kind::Index) {
        if (!slot || slot->type != core::AttributeType::L) {
            slot = std::make_shared<core::AttributeValue>();
            slot->type = core::AttributeType::L;
            slot->value = core::ListValue{};
        }
        auto& l = std::get<core::ListValue>(slot->value);
        l.push_back(nullptr);
        project_merge(l.back(), segs, i + 1, names, value);
    } else {
        std::string name = seg.name;
        if (!name.empty() && name[0] == '#') {
            auto it = names.find(name);
            if (it != names.end()) name = it->second;
        }
        if (!slot || slot->type != core::AttributeType::M) {
            slot = std::make_shared<core::AttributeValue>();
            slot->type = core::AttributeType::M;
            slot->value = core::MapValue{};
        }
        auto& m = std::get<core::MapValue>(slot->value);
        auto& child = m[core::String(name)];
        project_merge(child, segs, i + 1, names, value);
    }
}

// Applies a ProjectionExpression (comma-separated document paths) to an item,
// returning a projected sub-document. Sets `unsupported` if a path is malformed.
AttributeMap apply_projection(const AttributeMap& item, const std::string& projection,
                              const NameMap& names, bool& unsupported) {
    AttributeMap out;
    size_t start = 0;
    while (start <= projection.size()) {
        size_t comma = projection.find(',', start);
        std::string token = projection.substr(start, comma == std::string::npos ? std::string::npos : comma - start);
        size_t b = token.find_first_not_of(" \t\r\n");
        size_t e = token.find_last_not_of(" \t\r\n");
        if (b != std::string::npos) {
            token = token.substr(b, e - b + 1);
            auto path = expressions::parse_single_path(token);
            if (!path || path->segments.empty() ||
                path->segments.front().kind != expressions::PathSegment::Kind::Name) {
                unsupported = true;
            } else {
                auto value = expressions::Evaluator::resolve_path(*path, item, names);
                if (value) {
                    std::string root = path->segments.front().name;
                    if (!root.empty() && root[0] == '#') {
                        auto it = names.find(root);
                        if (it != names.end()) root = it->second;
                    }
                    project_merge(out[root], path->segments, 1, names, value);
                }
            }
        }
        if (comma == std::string::npos) break;
        start = comma + 1;
    }
    return out;
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

// ---- Key condition expression parsing (for Query) -----------------------

struct SortCondition {
    bool present = false;
    std::string op;  // "=", "<", "<=", ">", ">=", "BETWEEN", "begins_with"
    std::shared_ptr<core::AttributeValue> v1;
    std::shared_ptr<core::AttributeValue> v2;  // for BETWEEN
};

struct KeyConditionParse {
    bool ok = false;
    std::string error;
    std::string pk_name;
    std::shared_ptr<core::AttributeValue> pk_value;
    std::string sk_name;
    SortCondition sort;
};

KeyConditionParse parse_key_condition(const std::string& expr, const NameMap& names,
                                      const AttributeMap& values) {
    using expressions::TokenType;
    KeyConditionParse out;
    expressions::Lexer lexer(expr);
    auto toks = lexer.tokenize();
    size_t pos = 0;
    auto peek = [&]() -> const expressions::Token& { return toks[pos]; };
    auto resolve_name = [&](const expressions::Token& t) -> std::optional<std::string> {
        if (t.type == TokenType::IDENTIFIER) return t.value;
        if (t.type == TokenType::PLACEHOLDER_NAME) {
            auto it = names.find(t.value);
            if (it != names.end()) return it->second;
        }
        return std::nullopt;
    };
    auto resolve_value = [&](const expressions::Token& t) -> std::shared_ptr<core::AttributeValue> {
        if (t.type != TokenType::PLACEHOLDER_VALUE) return nullptr;
        auto it = values.find(t.value);
        return it != values.end() ? it->second : nullptr;
    };

    // Partition key: <pk> = <value>
    auto pk = resolve_name(peek());
    if (!pk) { out.error = "KeyConditionExpression must start with a partition key"; return out; }
    pos++;
    if (peek().type != TokenType::OPERATOR || peek().value != "=") {
        out.error = "Partition key condition must use '='"; return out;
    }
    pos++;
    auto pkv = resolve_value(peek());
    if (!pkv) { out.error = "Partition key value placeholder not found"; return out; }
    pos++;
    out.pk_name = *pk;
    out.pk_value = pkv;

    if (peek().type == TokenType::END_OF_FILE) { out.ok = true; return out; }

    // Sort key clause: AND ...
    if (!(peek().type == TokenType::KEYWORD && peek().value == "AND")) {
        out.error = "Expected AND before sort key condition"; return out;
    }
    pos++;

    // begins_with(sk, :v)
    if (peek().type == TokenType::IDENTIFIER) {
        std::string ident = peek().value;
        std::string upper = ident;
        std::transform(upper.begin(), upper.end(), upper.begin(), [](unsigned char c){ return static_cast<char>(std::toupper(c)); });
        if (upper == "BEGINS_WITH") {
            pos++;
            if (peek().type != TokenType::OPEN_PAREN) { out.error = "begins_with expects '('"; return out; }
            pos++;
            auto sk = resolve_name(peek());
            if (!sk) { out.error = "begins_with sort-key name not found"; return out; }
            pos++;
            if (peek().type != TokenType::COMMA) { out.error = "begins_with expects two args"; return out; }
            pos++;
            auto v = resolve_value(peek());
            if (!v) { out.error = "begins_with value not found"; return out; }
            pos++;
            if (peek().type != TokenType::CLOSE_PAREN) { out.error = "begins_with missing ')'"; return out; }
            pos++;
            out.sk_name = *sk;
            out.sort = {true, "begins_with", v, nullptr};
            if (peek().type != TokenType::END_OF_FILE) { out.error = "Trailing tokens in KeyConditionExpression"; return out; }
            out.ok = true;
            return out;
        }
    }

    // <sk> <op> <value>  or  <sk> BETWEEN <v1> AND <v2>
    auto sk = resolve_name(peek());
    if (!sk) { out.error = "Sort key name not found"; return out; }
    pos++;
    out.sk_name = *sk;
    if (peek().type == TokenType::OPERATOR) {
        std::string op = peek().value;
        if (op != "=" && op != "<" && op != "<=" && op != ">" && op != ">=") {
            out.error = "Unsupported sort key operator"; return out;
        }
        pos++;
        auto v = resolve_value(peek());
        if (!v) { out.error = "Sort key value not found"; return out; }
        pos++;
        out.sort = {true, op, v, nullptr};
    } else if (peek().type == TokenType::KEYWORD && peek().value == "BETWEEN") {
        pos++;
        auto v1 = resolve_value(peek());
        if (!v1) { out.error = "BETWEEN low value not found"; return out; }
        pos++;
        if (!(peek().type == TokenType::KEYWORD && peek().value == "AND")) { out.error = "BETWEEN expects AND"; return out; }
        pos++;
        auto v2 = resolve_value(peek());
        if (!v2) { out.error = "BETWEEN high value not found"; return out; }
        pos++;
        out.sort = {true, "BETWEEN", v1, v2};
    } else {
        out.error = "Invalid sort key condition"; return out;
    }
    if (peek().type != TokenType::END_OF_FILE) { out.error = "Trailing tokens in KeyConditionExpression"; return out; }
    out.ok = true;
    return out;
}

// Compares two scalar attribute values for the sort-key operators.
bool sort_matches(const std::shared_ptr<core::AttributeValue>& item_val, const SortCondition& cond) {
    if (!item_val) return false;
    AttributeMap empty_names_item;  // unused; we compare directly via evaluator-free logic
    auto cmp = [](const std::shared_ptr<core::AttributeValue>& a,
                  const std::shared_ptr<core::AttributeValue>& b, std::string_view op) {
        // Reuse the evaluator's comparison through a tiny condition AST is overkill;
        // compare String/Number/Binary directly.
        if (!a || !b || a->type != b->type) return false;
        auto apply = [&](auto l, auto r) {
            if (op == "=") return l == r;
            if (op == "<") return l < r;
            if (op == "<=") return l <= r;
            if (op == ">") return l > r;
            if (op == ">=") return l >= r;
            return false;
        };
        if (a->type == core::AttributeType::S) {
            return apply(std::get<core::String>(a->value), std::get<core::String>(b->value));
        }
        if (a->type == core::AttributeType::N) {
            long double l = std::strtold(std::get<core::String>(a->value).c_str(), nullptr);
            long double r = std::strtold(std::get<core::String>(b->value).c_str(), nullptr);
            return apply(l, r);
        }
        if (a->type == core::AttributeType::B) {
            return apply(std::get<std::pmr::vector<uint8_t>>(a->value),
                         std::get<std::pmr::vector<uint8_t>>(b->value));
        }
        return false;
    };
    if (cond.op == "begins_with") {
        if (item_val->type == core::AttributeType::S && cond.v1->type == core::AttributeType::S) {
            return std::get<core::String>(item_val->value).starts_with(std::get<core::String>(cond.v1->value));
        }
        return false;
    }
    if (cond.op == "BETWEEN") {
        return cmp(item_val, cond.v1, ">=") && cmp(item_val, cond.v2, "<=");
    }
    return cmp(item_val, cond.v1, cond.op);
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

ApiResult handle_delete_table(engine::TableManager& tables, engine::StorageEngine& storage,
                              simdjson::dom::element doc) {
    std::string_view name;
    if (doc["TableName"].get_string().get(name) != simdjson::SUCCESS) {
        return error(400, "ValidationException", "TableName is required");
    }
    auto removed = tables.delete_table(name);
    if (!removed) {
        return error(400, "ResourceNotFoundException",
                     "Requested resource not found: Table: " + std::string(name) + " not found");
    }
    storage.drop_table(std::string(name));
    return ok("{\"TableDescription\":" + serialize_table_description(*removed) + "}");
}

ApiResult handle_update_table(engine::TableManager& tables, simdjson::dom::element doc) {
    std::string_view name;
    if (doc["TableName"].get_string().get(name) != simdjson::SUCCESS) {
        return error(400, "ValidationException", "TableName is required");
    }
    auto def = tables.describe_table(name);
    if (!def) {
        return error(400, "ResourceNotFoundException",
                     "Requested resource not found: Table: " + std::string(name) + " not found");
    }
    // Minimal UpdateTable: the table stays ACTIVE; billing-mode/throughput changes
    // are accepted as a no-op since this engine does not provision capacity.
    return ok("{\"TableDescription\":" + serialize_table_description(*def) + "}");
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
        if (valid.error() == engine::ValidationError::EmptyKeyAttribute) {
            return error(400, "ValidationException",
                         "One or more parameter values were invalid: An AttributeValue may not contain an empty string for key attribute");
        }
        return error(400, "ValidationException", "Item failed validation");
    }

    std::string_view condition_view;
    const bool has_condition =
        doc["ConditionExpression"].get_string().get(condition_view) == simdjson::SUCCESS;
    const std::string condition(condition_view);
    NameMap names = parse_expr_names(doc);
    auto values_opt = parse_expr_values(doc);
    if (!values_opt) return error(400, "ValidationException", "Invalid ExpressionAttributeValues");

    std::string_view return_values;
    auto rv_rc = doc["ReturnValues"].get_string().get(return_values); (void)rv_rc;

    bool parse_error = false;
    bool condition_failed = false;
    auto outcome = storage.mutate(std::string(name), engine::encode_primary_key(*def, *item),
                                  [&](const AttributeMap* current) -> Mutation {
        if (has_condition) {
            AttributeMap empty;
            const AttributeMap& target = current ? *current : empty;
            int r = evaluate_boolean_expr(condition, target, names, *values_opt);
            if (r < 0) { parse_error = true; return {MutationKind::None, {}}; }
            if (r == 0) { condition_failed = true; return {MutationKind::None, {}}; }
        }
        return {MutationKind::Put, *item};
    });

    if (parse_error) return error(400, "ValidationException", "Invalid ConditionExpression");
    if (condition_failed) return error(400, "ConditionalCheckFailedException", "The conditional request failed");

    if (return_values == "ALL_OLD" && outcome.previous) {
        return ok("{\"Attributes\":" + json::JsonSerializer::serialize_item(*outcome.previous) + "}");
    }
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
    if (!key_values_non_empty(*def, *key_map)) {
        return error(400, "ValidationException",
                     "One or more parameter values were invalid: empty key attribute value");
    }

    std::string key = engine::encode_primary_key(*def, *key_map);
    auto item = storage.get(std::string(name), key);
    if (!item) {
        return ok("{}");
    }

    std::string_view projection;
    if (doc["ProjectionExpression"].get_string().get(projection) == simdjson::SUCCESS) {
        bool unsupported = false;
        NameMap names = parse_expr_names(doc);
        AttributeMap projected = apply_projection(*item, std::string(projection), names, unsupported);
        if (unsupported) {
            return error(400, "ValidationException",
                         "Document-path ProjectionExpression is not yet supported; use top-level attributes");
        }
        return ok("{\"Item\":" + json::JsonSerializer::serialize_item(projected) + "}");
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
    if (!key_values_non_empty(*def, *key_map)) {
        return error(400, "ValidationException",
                     "One or more parameter values were invalid: empty key attribute value");
    }

    std::string_view condition_view;
    const bool has_condition =
        doc["ConditionExpression"].get_string().get(condition_view) == simdjson::SUCCESS;
    const std::string condition(condition_view);
    NameMap names = parse_expr_names(doc);
    auto values_opt = parse_expr_values(doc);
    if (!values_opt) return error(400, "ValidationException", "Invalid ExpressionAttributeValues");

    std::string_view return_values;
    auto rv_rc = doc["ReturnValues"].get_string().get(return_values); (void)rv_rc;

    bool parse_error = false;
    bool condition_failed = false;
    auto outcome = storage.mutate(std::string(name), engine::encode_primary_key(*def, *key_map),
                                  [&](const AttributeMap* current) -> Mutation {
        if (has_condition) {
            AttributeMap empty;
            const AttributeMap& target = current ? *current : empty;
            int r = evaluate_boolean_expr(condition, target, names, *values_opt);
            if (r < 0) { parse_error = true; return {MutationKind::None, {}}; }
            if (r == 0) { condition_failed = true; return {MutationKind::None, {}}; }
        }
        return {MutationKind::Delete, {}};
    });

    if (parse_error) return error(400, "ValidationException", "Invalid ConditionExpression");
    if (condition_failed) return error(400, "ConditionalCheckFailedException", "The conditional request failed");

    if (return_values == "ALL_OLD" && outcome.previous) {
        return ok("{\"Attributes\":" + json::JsonSerializer::serialize_item(*outcome.previous) + "}");
    }
    return ok("{}");
}

ApiResult handle_update_item(engine::TableManager& tables, engine::StorageEngine& storage,
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
    if (!key_values_non_empty(*def, *key_map)) {
        return error(400, "ValidationException",
                     "One or more parameter values were invalid: empty key attribute value");
    }

    std::string_view update_view;
    const bool has_update =
        doc["UpdateExpression"].get_string().get(update_view) == simdjson::SUCCESS;
    const std::string update_expr(update_view);

    std::string_view condition_view;
    const bool has_condition =
        doc["ConditionExpression"].get_string().get(condition_view) == simdjson::SUCCESS;
    const std::string condition(condition_view);

    NameMap names = parse_expr_names(doc);
    auto values_opt = parse_expr_values(doc);
    if (!values_opt) return error(400, "ValidationException", "Invalid ExpressionAttributeValues");

    std::string_view return_values;
    auto rv_rc = doc["ReturnValues"].get_string().get(return_values); (void)rv_rc;

    bool parse_error = false;
    bool condition_failed = false;
    std::string update_error;
    AttributeMap new_item;

    auto outcome = storage.mutate(std::string(name), engine::encode_primary_key(*def, *key_map),
                                  [&](const AttributeMap* current) -> Mutation {
        AttributeMap working = current ? *current : AttributeMap{};
        // Ensure key attributes are present (UpdateItem creates the item if absent).
        for (const auto& [k, v] : *key_map) working[k] = v;

        if (has_condition) {
            AttributeMap empty;
            const AttributeMap& target = current ? *current : empty;
            int r = evaluate_boolean_expr(condition, target, names, *values_opt);
            if (r < 0) { parse_error = true; return {MutationKind::None, {}}; }
            if (r == 0) { condition_failed = true; return {MutationKind::None, {}}; }
        }

        if (has_update) {
            auto res = expressions::apply_update_expression(update_expr, working, names, *values_opt);
            if (!res.ok) { update_error = res.error; return {MutationKind::None, {}}; }
        }
        new_item = working;
        return {MutationKind::Put, working};
    });

    if (parse_error) return error(400, "ValidationException", "Invalid ConditionExpression");
    if (condition_failed) return error(400, "ConditionalCheckFailedException", "The conditional request failed");
    if (!update_error.empty()) return error(400, "ValidationException", update_error);

    if (return_values == "ALL_NEW" || return_values == "UPDATED_NEW") {
        return ok("{\"Attributes\":" + json::JsonSerializer::serialize_item(new_item) + "}");
    }
    if ((return_values == "ALL_OLD" || return_values == "UPDATED_OLD") && outcome.previous) {
        return ok("{\"Attributes\":" + json::JsonSerializer::serialize_item(*outcome.previous) + "}");
    }
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

    std::string_view filter_view;
    const bool has_filter = doc["FilterExpression"].get_string().get(filter_view) == simdjson::SUCCESS;
    const std::string filter(filter_view);
    std::string_view projection_view;
    const bool has_projection = doc["ProjectionExpression"].get_string().get(projection_view) == simdjson::SUCCESS;
    NameMap names = parse_expr_names(doc);
    auto values_opt = parse_expr_values(doc);
    if (!values_opt) return error(400, "ValidationException", "Invalid ExpressionAttributeValues");

    std::vector<AttributeMap> output;
    for (const auto& item : result.items) {
        if (has_filter) {
            int r = evaluate_boolean_expr(filter, item, names, *values_opt);
            if (r < 0) return error(400, "ValidationException", "Invalid FilterExpression");
            if (r == 0) continue;
        }
        if (has_projection) {
            bool unsupported = false;
            AttributeMap projected = apply_projection(item, std::string(projection_view), names, unsupported);
            if (unsupported) return error(400, "ValidationException",
                                          "Document-path ProjectionExpression is not yet supported");
            output.push_back(std::move(projected));
        } else {
            output.push_back(item);
        }
    }

    std::string out = "{\"Items\":" + items_to_json(output) +
                      ",\"Count\":" + std::to_string(output.size()) +
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

    NameMap names = parse_expr_names(doc);
    auto values_opt = parse_expr_values(doc);
    if (!values_opt) return error(400, "ValidationException", "Invalid ExpressionAttributeValues");

    AttributeMap pk_condition;            // partition-key equality fed to storage
    SortCondition sort;                   // optional sort-key condition applied here
    std::string sk_name;

    std::string_view kce_view;
    if (doc["KeyConditionExpression"].get_string().get(kce_view) == simdjson::SUCCESS) {
        auto parsed = parse_key_condition(std::string(kce_view), names, *values_opt);
        if (!parsed.ok) return error(400, "ValidationException",
                                     parsed.error.empty() ? "Invalid KeyConditionExpression" : parsed.error);
        pk_condition[parsed.pk_name] = parsed.pk_value;
        sort = parsed.sort;
        sk_name = parsed.sk_name;
    } else {
        // Legacy KeyConditions with EQ comparison operators.
        simdjson::dom::object conditions_obj;
        if (doc["KeyConditions"].get_object().get(conditions_obj) != simdjson::SUCCESS) {
            return error(400, "ValidationException",
                         "Query requires KeyConditionExpression or KeyConditions");
        }
        for (auto field : conditions_obj) {
            std::string_view op;
            if (field.value["ComparisonOperator"].get_string().get(op) != simdjson::SUCCESS || op != "EQ") {
                return error(400, "ValidationException",
                             "Only the EQ comparison operator is supported in legacy KeyConditions");
            }
            simdjson::dom::array vals;
            if (field.value["AttributeValueList"].get_array().get(vals) != simdjson::SUCCESS) {
                return error(400, "ValidationException", "AttributeValueList is required for each condition");
            }
            auto it = vals.begin();
            if (it == vals.end()) {
                return error(400, "ValidationException", "EQ requires exactly one attribute value");
            }
            try {
                pk_condition[std::string(field.key)] =
                    std::make_shared<core::AttributeValue>(json::JsonParser::parse_attribute_value(*it));
            } catch (const std::exception&) {
                return error(400, "ValidationException", "Invalid attribute value in KeyConditions");
            }
        }
        if (pk_condition.empty()) {
            return error(400, "ValidationException", "KeyConditions must not be empty");
        }
    }

    bool forward = true;
    auto sif_rc = doc["ScanIndexForward"].get_bool().get(forward); (void)sif_rc;

    // Fetch the whole partition (ascending), then apply sort condition, ordering,
    // pagination, filter, projection and limit at the handler level.
    auto result = storage.query(std::string(name), pk_condition, std::nullopt, 0);
    std::vector<AttributeMap> matched;
    for (auto& item : result.items) {
        if (sort.present) {
            auto it = item.find(sk_name);
            if (it == item.end() || !sort_matches(it->second, sort)) continue;
        }
        matched.push_back(std::move(item));
    }
    if (!forward) std::reverse(matched.begin(), matched.end());

    // ExclusiveStartKey: skip everything up to and including the supplied key.
    auto esk = read_start_key(doc, *def);
    size_t begin_idx = 0;
    if (esk) {
        for (; begin_idx < matched.size(); ++begin_idx) {
            std::string ek = engine::encode_primary_key(*def, matched[begin_idx]);
            bool past = forward ? (ek > *esk) : (ek < *esk);
            if (past) break;
        }
    }

    size_t limit = read_limit(doc).value_or(0);
    std::string_view filter_view;
    const bool has_filter = doc["FilterExpression"].get_string().get(filter_view) == simdjson::SUCCESS;
    const std::string filter(filter_view);
    std::string_view projection_view;
    const bool has_projection = doc["ProjectionExpression"].get_string().get(projection_view) == simdjson::SUCCESS;

    std::vector<AttributeMap> output;
    size_t examined = 0;
    std::optional<std::string> last_evaluated;
    for (size_t i = begin_idx; i < matched.size(); ++i) {
        if (limit != 0 && examined == limit) {
            last_evaluated = key_only_json(*def, matched[i - 1]);
            break;
        }
        ++examined;
        const auto& item = matched[i];
        if (has_filter) {
            int r = evaluate_boolean_expr(filter, item, names, *values_opt);
            if (r < 0) return error(400, "ValidationException", "Invalid FilterExpression");
            if (r == 0) continue;
        }
        if (has_projection) {
            bool unsupported = false;
            AttributeMap projected = apply_projection(item, std::string(projection_view), names, unsupported);
            if (unsupported) return error(400, "ValidationException",
                                          "Document-path ProjectionExpression is not yet supported");
            output.push_back(std::move(projected));
        } else {
            output.push_back(item);
        }
    }

    std::string out = "{\"Items\":" + items_to_json(output) +
                      ",\"Count\":" + std::to_string(output.size()) +
                      ",\"ScannedCount\":" + std::to_string(examined);
    if (last_evaluated) {
        out += ",\"LastEvaluatedKey\":" + *last_evaluated;
    }
    out += "}";
    return ok(std::move(out));
}

// ---- Batch & transaction handlers ---------------------------------------

ApiResult handle_batch_write_item(engine::TableManager& tables, engine::StorageEngine& storage,
                                  simdjson::dom::element doc) {
    simdjson::dom::object request_items;
    if (doc["RequestItems"].get_object().get(request_items) != simdjson::SUCCESS) {
        return error(400, "ValidationException", "RequestItems is required");
    }
    // Validate everything first so a bad request writes nothing.
    struct Op { std::string table; bool is_delete; std::string key; AttributeMap item; };
    std::vector<Op> ops;
    for (auto table_field : request_items) {
        std::string table_name(table_field.key);
        auto def = tables.describe_table(table_name);
        if (!def) {
            return error(400, "ResourceNotFoundException",
                         "Requested resource not found: Table: " + table_name + " not found");
        }
        simdjson::dom::array reqs;
        if (table_field.value.get_array().get(reqs) != simdjson::SUCCESS) {
            return error(400, "ValidationException", "Each table entry must be a list of write requests");
        }
        for (auto req : reqs) {
            simdjson::dom::element put_el;
            simdjson::dom::element del_el;
            if (req["PutRequest"].get(put_el) == simdjson::SUCCESS) {
                simdjson::dom::element item_el;
                if (put_el["Item"].get(item_el) != simdjson::SUCCESS) {
                    return error(400, "ValidationException", "PutRequest requires Item");
                }
                auto item = parse_attribute_map(item_el);
                if (!item || !item_has_all_keys(*def, *item)) {
                    return error(400, "ValidationException", "PutRequest item missing key attributes");
                }
                auto valid = engine::ItemValidator::validate_item_standard(*item, *def);
                if (!valid) return error(400, "ValidationException", "PutRequest item failed validation");
                ops.push_back({table_name, false, engine::encode_primary_key(*def, *item), std::move(*item)});
            } else if (req["DeleteRequest"].get(del_el) == simdjson::SUCCESS) {
                simdjson::dom::element key_el;
                if (del_el["Key"].get(key_el) != simdjson::SUCCESS) {
                    return error(400, "ValidationException", "DeleteRequest requires Key");
                }
                auto key_map = parse_attribute_map(key_el);
                if (!key_map || !item_has_all_keys(*def, *key_map)) {
                    return error(400, "ValidationException", "DeleteRequest key missing attributes");
                }
                ops.push_back({table_name, true, engine::encode_primary_key(*def, *key_map), {}});
            } else {
                return error(400, "ValidationException", "Each request must be a PutRequest or DeleteRequest");
            }
        }
    }
    for (auto& op : ops) {
        if (op.is_delete) storage.remove(op.table, op.key);
        else storage.put(op.table, op.key, op.item);
    }
    return ok("{\"UnprocessedItems\":{}}");
}

ApiResult handle_batch_get_item(engine::TableManager& tables, engine::StorageEngine& storage,
                                simdjson::dom::element doc) {
    simdjson::dom::object request_items;
    if (doc["RequestItems"].get_object().get(request_items) != simdjson::SUCCESS) {
        return error(400, "ValidationException", "RequestItems is required");
    }
    std::string responses = "{";
    bool first_table = true;
    for (auto table_field : request_items) {
        std::string table_name(table_field.key);
        auto def = tables.describe_table(table_name);
        if (!def) {
            return error(400, "ResourceNotFoundException",
                         "Requested resource not found: Table: " + table_name + " not found");
        }
        simdjson::dom::array keys;
        if (table_field.value["Keys"].get_array().get(keys) != simdjson::SUCCESS) {
            return error(400, "ValidationException", "Each table entry requires Keys");
        }
        std::vector<AttributeMap> items;
        for (auto key_el : keys) {
            auto key_map = parse_attribute_map(key_el);
            if (!key_map || !item_has_all_keys(*def, *key_map)) {
                return error(400, "ValidationException", "A key does not match the table schema");
            }
            auto item = storage.get(table_name, engine::encode_primary_key(*def, *key_map));
            if (item) items.push_back(*item);
        }
        if (!first_table) responses += ",";
        responses += "\"" + table_name + "\":" + items_to_json(items);
        first_table = false;
    }
    responses += "}";
    return ok("{\"Responses\":" + responses + ",\"UnprocessedKeys\":{}}");
}

ApiResult handle_transact_write_items(engine::TableManager& tables, engine::StorageEngine& storage,
                                      simdjson::dom::element doc) {
    simdjson::dom::array transact_items;
    if (doc["TransactItems"].get_array().get(transact_items) != simdjson::SUCCESS) {
        return error(400, "ValidationException", "TransactItems is required");
    }
    // Two-phase: validate keys/conditions, then apply. Conditions are checked
    // against a current snapshot so the all-or-nothing contract holds for the
    // common case (no concurrent writer to the same keys mid-transaction).
    struct Action { int kind; std::string table; std::string key; AttributeMap item; AttributeMap key_map;
                    bool has_condition; std::string condition; NameMap names; AttributeMap values; std::string update_expr; bool has_update; };
    std::vector<Action> actions;

    auto load_expr = [](simdjson::dom::element el, Action& a) {
        std::string_view c;
        a.has_condition = el["ConditionExpression"].get_string().get(c) == simdjson::SUCCESS;
        a.condition = std::string(c);
        a.names = parse_expr_names(el);
        auto v = parse_expr_values(el);
        if (v) a.values = *v;
    };

    for (auto entry : transact_items) {
        simdjson::dom::element sub;
        Action a{};
        if (entry["Put"].get(sub) == simdjson::SUCCESS) {
            a.kind = 0;
            std::string_view t;
            if (sub["TableName"].get_string().get(t) != simdjson::SUCCESS) return error(400, "ValidationException", "Put requires TableName");
            a.table = std::string(t);
            auto def = tables.describe_table(a.table);
            if (!def) return error(400, "ResourceNotFoundException", "Table not found: " + a.table);
            simdjson::dom::element item_el;
            if (sub["Item"].get(item_el) != simdjson::SUCCESS) return error(400, "ValidationException", "Put requires Item");
            auto item = parse_attribute_map(item_el);
            if (!item || !item_has_all_keys(*def, *item)) return error(400, "ValidationException", "Put item missing keys");
            a.key = engine::encode_primary_key(*def, *item);
            a.item = std::move(*item);
            load_expr(sub, a);
        } else if (entry["Delete"].get(sub) == simdjson::SUCCESS) {
            a.kind = 1;
            std::string_view t;
            if (sub["TableName"].get_string().get(t) != simdjson::SUCCESS) return error(400, "ValidationException", "Delete requires TableName");
            a.table = std::string(t);
            auto def = tables.describe_table(a.table);
            if (!def) return error(400, "ResourceNotFoundException", "Table not found: " + a.table);
            simdjson::dom::element key_el;
            if (sub["Key"].get(key_el) != simdjson::SUCCESS) return error(400, "ValidationException", "Delete requires Key");
            auto key_map = parse_attribute_map(key_el);
            if (!key_map || !item_has_all_keys(*def, *key_map)) return error(400, "ValidationException", "Delete key invalid");
            a.key = engine::encode_primary_key(*def, *key_map);
            a.key_map = *key_map;
            load_expr(sub, a);
        } else if (entry["Update"].get(sub) == simdjson::SUCCESS) {
            a.kind = 2;
            std::string_view t;
            if (sub["TableName"].get_string().get(t) != simdjson::SUCCESS) return error(400, "ValidationException", "Update requires TableName");
            a.table = std::string(t);
            auto def = tables.describe_table(a.table);
            if (!def) return error(400, "ResourceNotFoundException", "Table not found: " + a.table);
            simdjson::dom::element key_el;
            if (sub["Key"].get(key_el) != simdjson::SUCCESS) return error(400, "ValidationException", "Update requires Key");
            auto key_map = parse_attribute_map(key_el);
            if (!key_map || !item_has_all_keys(*def, *key_map)) return error(400, "ValidationException", "Update key invalid");
            a.key = engine::encode_primary_key(*def, *key_map);
            a.key_map = *key_map;
            std::string_view u;
            a.has_update = sub["UpdateExpression"].get_string().get(u) == simdjson::SUCCESS;
            a.update_expr = std::string(u);
            load_expr(sub, a);
        } else if (entry["ConditionCheck"].get(sub) == simdjson::SUCCESS) {
            a.kind = 3;
            std::string_view t;
            if (sub["TableName"].get_string().get(t) != simdjson::SUCCESS) return error(400, "ValidationException", "ConditionCheck requires TableName");
            a.table = std::string(t);
            auto def = tables.describe_table(a.table);
            if (!def) return error(400, "ResourceNotFoundException", "Table not found: " + a.table);
            simdjson::dom::element key_el;
            if (sub["Key"].get(key_el) != simdjson::SUCCESS) return error(400, "ValidationException", "ConditionCheck requires Key");
            auto key_map = parse_attribute_map(key_el);
            if (!key_map || !item_has_all_keys(*def, *key_map)) return error(400, "ValidationException", "ConditionCheck key invalid");
            a.key = engine::encode_primary_key(*def, *key_map);
            a.key_map = *key_map;
            load_expr(sub, a);
            if (!a.has_condition) return error(400, "ValidationException", "ConditionCheck requires a ConditionExpression");
        } else {
            return error(400, "ValidationException", "Each TransactItem must be Put/Delete/Update/ConditionCheck");
        }
        actions.push_back(std::move(a));
    }

    // Phase 1: verify every condition AND fully evaluate every UpdateExpression
    // against the current snapshot. Nothing is written in this phase, so any
    // validation failure (bad condition, malformed/unsatisfiable update) aborts the
    // whole transaction before a single write lands — preserving all-or-nothing even
    // when an Update later in the list is invalid.
    std::vector<AttributeMap> precomputed_updates(actions.size());
    for (size_t i = 0; i < actions.size(); ++i) {
        const auto& a = actions[i];
        if (a.has_condition) {
            auto current = storage.get(a.table, a.key);
            AttributeMap empty;
            const AttributeMap& target = current ? *current : empty;
            int r = evaluate_boolean_expr(a.condition, target, a.names, a.values);
            if (r < 0) return error(400, "ValidationException", "Invalid ConditionExpression in transaction");
            if (r == 0) return error(400, "TransactionCanceledException",
                                     "Transaction cancelled, please refer cancellation reasons for specific reasons [ConditionalCheckFailed]");
        }
        if (a.kind == 2) {  // Update: compute the resulting item now so a bad expr aborts pre-write.
            auto current = storage.get(a.table, a.key);
            AttributeMap working = current ? *current : AttributeMap{};
            for (const auto& [k, v] : a.key_map) working[k] = v;
            if (a.has_update) {
                auto res = expressions::apply_update_expression(a.update_expr, working, a.names, a.values);
                if (!res.ok) return error(400, "ValidationException", res.error);
            }
            precomputed_updates[i] = std::move(working);
        }
    }

    // Phase 2: apply all writes. Updates use the item computed in phase 1.
    for (size_t i = 0; i < actions.size(); ++i) {
        const auto& a = actions[i];
        if (a.kind == 0) {
            storage.put(a.table, a.key, a.item);
        } else if (a.kind == 1) {
            storage.remove(a.table, a.key);
        } else if (a.kind == 2) {
            storage.put(a.table, a.key, precomputed_updates[i]);
        }
        // kind == 3 (ConditionCheck) performs no write.
    }
    return ok("{}");
}

ApiResult handle_transact_get_items(engine::TableManager& tables, engine::StorageEngine& storage,
                                    simdjson::dom::element doc) {
    simdjson::dom::array transact_items;
    if (doc["TransactItems"].get_array().get(transact_items) != simdjson::SUCCESS) {
        return error(400, "ValidationException", "TransactItems is required");
    }
    std::string responses = "[";
    bool first = true;
    for (auto entry : transact_items) {
        simdjson::dom::element get_el;
        if (entry["Get"].get(get_el) != simdjson::SUCCESS) {
            return error(400, "ValidationException", "Each TransactItem must contain Get");
        }
        std::string_view t;
        if (get_el["TableName"].get_string().get(t) != simdjson::SUCCESS) {
            return error(400, "ValidationException", "Get requires TableName");
        }
        std::string table_name(t);
        auto def = tables.describe_table(table_name);
        if (!def) return error(400, "ResourceNotFoundException", "Table not found: " + table_name);
        simdjson::dom::element key_el;
        if (get_el["Key"].get(key_el) != simdjson::SUCCESS) {
            return error(400, "ValidationException", "Get requires Key");
        }
        auto key_map = parse_attribute_map(key_el);
        if (!key_map || !item_has_all_keys(*def, *key_map)) {
            return error(400, "ValidationException", "Get key does not match schema");
        }
        auto item = storage.get(table_name, engine::encode_primary_key(*def, *key_map));
        if (!first) responses += ",";
        if (item) responses += "{\"Item\":" + json::JsonSerializer::serialize_item(*item) + "}";
        else responses += "{}";
        first = false;
    }
    responses += "]";
    return ok("{\"Responses\":" + responses + "}");
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

    try {
        switch (op) {
            case Operation::CreateTable:        return handle_create_table(tables, doc);
            case Operation::DescribeTable:      return handle_describe_table(tables, doc);
            case Operation::DeleteTable:        return handle_delete_table(tables, storage, doc);
            case Operation::UpdateTable:        return handle_update_table(tables, doc);
            case Operation::ListTables:         return handle_list_tables(tables);
            case Operation::PutItem:            return handle_put_item(tables, storage, doc);
            case Operation::GetItem:            return handle_get_item(tables, storage, doc);
            case Operation::DeleteItem:         return handle_delete_item(tables, storage, doc);
            case Operation::UpdateItem:         return handle_update_item(tables, storage, doc);
            case Operation::Scan:               return handle_scan(tables, storage, doc);
            case Operation::Query:              return handle_query(tables, storage, doc);
            case Operation::BatchWriteItem:     return handle_batch_write_item(tables, storage, doc);
            case Operation::BatchGetItem:       return handle_batch_get_item(tables, storage, doc);
            case Operation::TransactWriteItems: return handle_transact_write_items(tables, storage, doc);
            case Operation::TransactGetItems:   return handle_transact_get_items(tables, storage, doc);
            case Operation::Unknown:
                return error(400, "UnknownOperationException",
                             "Unknown operation requested of cynamoDB");
            default:
                // The operation is a recognized DynamoDB target, just not built yet.
                // 501 lets SDK feature-detection distinguish this from a typo'd target.
                return error(501, "NotImplementedException",
                             "Operation is recognized but not yet implemented by cynamoDB: " +
                                 std::string(ApiDispatcher::to_string(op)));
        }
    } catch (const std::exception& e) {
        return error(400, "ValidationException", std::string("Request could not be processed: ") + e.what());
    }
}

}  // namespace cynamodb::api
