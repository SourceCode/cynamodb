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
#include <chrono>
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

    auto projection_str = [](core::ProjectionType t) -> std::string {
        switch (t) {
            case core::ProjectionType::KEYS_ONLY: return "KEYS_ONLY";
            case core::ProjectionType::INCLUDE: return "INCLUDE";
            default: return "ALL";
        }
    };
    auto index_schema_json = [&](const std::vector<core::KeySchemaElement>& schema) {
        std::string s = "[";
        for (size_t i = 0; i < schema.size(); ++i) {
            if (i) s += ",";
            s += "{\"AttributeName\":\"" + schema[i].attribute_name + "\",\"KeyType\":\"" +
                 key_type_str(schema[i].key_type) + "\"}";
        }
        return s + "]";
    };

    if (!def.global_secondary_indexes.empty()) {
        out += ",\"GlobalSecondaryIndexes\":[";
        for (size_t i = 0; i < def.global_secondary_indexes.size(); ++i) {
            const auto& g = def.global_secondary_indexes[i];
            if (i) out += ",";
            out += "{\"IndexName\":\"" + g.index_name + "\",\"IndexStatus\":\"ACTIVE\",\"KeySchema\":" +
                   index_schema_json(g.key_schema) + ",\"Projection\":{\"ProjectionType\":\"" +
                   projection_str(g.projection.projection_type) + "\"}}";
        }
        out += "]";
    }
    if (!def.local_secondary_indexes.empty()) {
        out += ",\"LocalSecondaryIndexes\":[";
        for (size_t i = 0; i < def.local_secondary_indexes.size(); ++i) {
            const auto& l = def.local_secondary_indexes[i];
            if (i) out += ",";
            out += "{\"IndexName\":\"" + l.index_name + "\",\"KeySchema\":" +
                   index_schema_json(l.key_schema) + ",\"Projection\":{\"ProjectionType\":\"" +
                   projection_str(l.projection.projection_type) + "\"}}";
        }
        out += "]";
    }

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

int64_t now_epoch_seconds() {
    return std::chrono::duration_cast<std::chrono::seconds>(
               std::chrono::system_clock::now().time_since_epoch())
        .count();
}

// True if the table has TTL enabled and the item's TTL attribute is a Number whose
// epoch-seconds value is at or before now (DynamoDB TTL semantics).
bool item_is_expired(const core::TableDefinition& def, const AttributeMap& item) {
    if (!def.ttl_specification || !def.ttl_specification->enabled) return false;
    auto it = item.find(def.ttl_specification->attribute_name);
    if (it == item.end() || !it->second || it->second->type != core::AttributeType::N) return false;
    char* end = nullptr;
    const auto& s = std::get<core::String>(it->second->value);
    long long ts = std::strtoll(s.c_str(), &end, 10);
    if (end != s.c_str() + s.size()) return false;  // non-integer TTL is ignored
    return ts <= now_epoch_seconds();
}

// ---- Secondary-index maintenance ----------------------------------------

// A unified view of a GSI or LSI for index maintenance / querying.
struct IndexSpec {
    std::string name;
    std::vector<core::KeySchemaElement> key_schema;  // [hash] or [hash, range]
    core::Projection projection;
};

std::vector<IndexSpec> all_indexes(const core::TableDefinition& def) {
    std::vector<IndexSpec> out;
    for (const auto& g : def.global_secondary_indexes) out.push_back({g.index_name, g.key_schema, g.projection});
    for (const auto& l : def.local_secondary_indexes) out.push_back({l.index_name, l.key_schema, l.projection});
    return out;
}

const IndexSpec* find_index(const std::vector<IndexSpec>& idxs, std::string_view name, IndexSpec& storage) {
    for (const auto& i : idxs) {
        if (i.name == name) { storage = i; return &storage; }
    }
    return nullptr;
}

// Index entries live in a reserved storage table; \x1d cannot appear in a real
// DynamoDB table name, so the namespace can never collide with a base table.
std::string index_storage_table(const std::string& base, const std::string& index_name) {
    return base + "\x1d" + index_name;
}

// Order-preserving storage key for an index entry: index hash, index range, then the
// base table keys (so non-unique index keys stay distinct and ordered by index range).
std::string encode_index_storage_key(const IndexSpec& idx, const core::TableDefinition& base,
                                     const AttributeMap& item) {
    std::string out;
    for (const auto& ks : idx.key_schema) {
        auto it = item.find(ks.attribute_name);
        if (it != item.end() && it->second) engine::encode_key_component(out, *it->second);
        else { out.push_back('\0'); out.push_back('\0'); }
    }
    for (const auto& ks : base.key_schema) {
        auto it = item.find(ks.attribute_name);
        if (it != item.end() && it->second) engine::encode_key_component(out, *it->second);
        else { out.push_back('\0'); out.push_back('\0'); }
    }
    return out;
}

// Projects an item into an index entry, or nullopt if the item lacks the index key
// attributes (a sparse index simply omits such items).
std::optional<AttributeMap> project_for_index(const AttributeMap& item, const IndexSpec& idx,
                                              const core::TableDefinition& base) {
    for (const auto& ks : idx.key_schema) {
        auto it = item.find(ks.attribute_name);
        if (it == item.end() || !it->second) return std::nullopt;  // sparse
    }
    AttributeMap out;
    auto copy_attr = [&](const std::string& name) {
        auto it = item.find(name);
        if (it != item.end()) out[name] = it->second;
    };
    // Base + index key attributes are always present in an index entry.
    for (const auto& ks : base.key_schema) copy_attr(ks.attribute_name);
    for (const auto& ks : idx.key_schema) copy_attr(ks.attribute_name);
    if (idx.projection.projection_type == core::ProjectionType::ALL) {
        out = item;
    } else if (idx.projection.projection_type == core::ProjectionType::INCLUDE) {
        for (const auto& a : idx.projection.non_key_attributes) copy_attr(a);
    }
    return out;
}

// Builds a LastEvaluatedKey JSON containing the index key + base key attributes.
std::string index_key_only_json(const IndexSpec& idx, const core::TableDefinition& def,
                                const AttributeMap& item) {
    AttributeMap key;
    for (const auto& ks : idx.key_schema) {
        auto it = item.find(ks.attribute_name);
        if (it != item.end()) key[it->first] = it->second;
    }
    for (const auto& ks : def.key_schema) {
        auto it = item.find(ks.attribute_name);
        if (it != item.end()) key[it->first] = it->second;
    }
    return json::JsonSerializer::serialize_item(key);
}

// Keeps every GSI/LSI consistent with a base-item change (old/new may be null).
void maintain_indexes(engine::StorageEngine& storage, const core::TableDefinition& def,
                      const AttributeMap* old_item, const AttributeMap* new_item) {
    auto idxs = all_indexes(def);
    if (idxs.empty()) return;
    for (const auto& idx : idxs) {
        const std::string tbl = index_storage_table(def.table_name, idx.name);
        if (old_item) {
            if (auto proj = project_for_index(*old_item, idx, def)) {
                storage.remove(tbl, encode_index_storage_key(idx, def, *old_item));
            }
        }
        if (new_item) {
            if (auto proj = project_for_index(*new_item, idx, def)) {
                storage.put(tbl, encode_index_storage_key(idx, def, *new_item), *proj);
            }
        }
    }
}

// ---- Streams helpers ----------------------------------------------------

AttributeMap extract_keys(const core::TableDefinition& def, const AttributeMap& item) {
    AttributeMap keys;
    for (const auto& ks : def.key_schema) {
        auto it = item.find(ks.attribute_name);
        if (it != item.end()) keys[it->first] = it->second;
    }
    return keys;
}

void emit_stream(streams::StreamManager* streams, const core::TableDefinition& def,
                 std::string_view event, const AttributeMap& keys,
                 const std::optional<AttributeMap>& old_image,
                 const std::optional<AttributeMap>& new_image) {
    if (streams == nullptr || !streams->is_stream_enabled(def)) return;
    streams->append_record(def, event, keys, old_image, new_image);
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
    // Purge each secondary index's storage namespace too.
    for (const auto& idx : all_indexes(*removed)) {
        storage.drop_table(index_storage_table(removed->table_name, idx.name));
    }
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

ApiResult handle_update_time_to_live(engine::TableManager& tables, simdjson::dom::element doc) {
    std::string_view name;
    if (doc["TableName"].get_string().get(name) != simdjson::SUCCESS) {
        return error(400, "ValidationException", "TableName is required");
    }
    simdjson::dom::element spec_el;
    if (doc["TimeToLiveSpecification"].get(spec_el) != simdjson::SUCCESS) {
        return error(400, "ValidationException", "TimeToLiveSpecification is required");
    }
    std::string_view attr;
    bool enabled = false;
    if (spec_el["AttributeName"].get_string().get(attr) != simdjson::SUCCESS) {
        return error(400, "ValidationException", "TimeToLiveSpecification.AttributeName is required");
    }
    if (spec_el["Enabled"].get_bool().get(enabled) != simdjson::SUCCESS) {
        return error(400, "ValidationException", "TimeToLiveSpecification.Enabled is required");
    }
    core::TimeToLiveSpecification spec;
    spec.attribute_name = std::string(attr);
    spec.enabled = enabled;
    auto updated = tables.set_ttl(name, spec);
    if (!updated) {
        return error(400, "ResourceNotFoundException",
                     "Requested resource not found: Table: " + std::string(name) + " not found");
    }
    return ok(std::string("{\"TimeToLiveSpecification\":{\"AttributeName\":\"") + std::string(attr) +
              "\",\"Enabled\":" + (enabled ? "true" : "false") + "}}");
}

ApiResult handle_describe_time_to_live(engine::TableManager& tables, simdjson::dom::element doc) {
    std::string_view name;
    if (doc["TableName"].get_string().get(name) != simdjson::SUCCESS) {
        return error(400, "ValidationException", "TableName is required");
    }
    auto def = tables.describe_table(name);
    if (!def) {
        return error(400, "ResourceNotFoundException",
                     "Requested resource not found: Table: " + std::string(name) + " not found");
    }
    std::string out = "{\"TimeToLiveDescription\":{";
    if (def->ttl_specification && def->ttl_specification->enabled) {
        out += "\"TimeToLiveStatus\":\"ENABLED\",\"AttributeName\":\"" +
               def->ttl_specification->attribute_name + "\"";
    } else {
        out += "\"TimeToLiveStatus\":\"DISABLED\"";
    }
    out += "}}";
    return ok(std::move(out));
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
                          simdjson::dom::element doc, streams::StreamManager* streams) {
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
        if (valid.error() == engine::ValidationError::InvalidNumber) {
            return error(400, "ValidationException",
                         "The parameter cannot be converted to a numeric value");
        }
        if (valid.error() == engine::ValidationError::EmptySet) {
            return error(400, "ValidationException",
                         "One or more parameter values were invalid: An attribute value of type set must not be empty");
        }
        if (valid.error() == engine::ValidationError::DuplicateSetValue) {
            return error(400, "ValidationException",
                         "One or more parameter values were invalid: Input collection contains duplicates");
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

    maintain_indexes(storage, *def, outcome.previous ? &*outcome.previous : nullptr, &*item);
    emit_stream(streams, *def, outcome.previous ? "MODIFY" : "INSERT", extract_keys(*def, *item),
                outcome.previous, *item);

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
    if (item_is_expired(*def, *item)) {
        // Lazily reap the expired item and report it as absent (DynamoDB TTL).
        storage.remove(std::string(name), key);
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
                             simdjson::dom::element doc, streams::StreamManager* streams) {
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

    if (outcome.previous) {
        maintain_indexes(storage, *def, &*outcome.previous, nullptr);
        emit_stream(streams, *def, "REMOVE", extract_keys(*def, *outcome.previous), outcome.previous, std::nullopt);
    }

    if (return_values == "ALL_OLD" && outcome.previous) {
        return ok("{\"Attributes\":" + json::JsonSerializer::serialize_item(*outcome.previous) + "}");
    }
    return ok("{}");
}

ApiResult handle_update_item(engine::TableManager& tables, engine::StorageEngine& storage,
                             simdjson::dom::element doc, streams::StreamManager* streams) {
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

    maintain_indexes(storage, *def, outcome.previous ? &*outcome.previous : nullptr, &new_item);
    emit_stream(streams, *def, outcome.previous ? "MODIFY" : "INSERT", extract_keys(*def, new_item),
                outcome.previous, new_item);

    if (return_values == "ALL_NEW" || return_values == "UPDATED_NEW") {
        return ok("{\"Attributes\":" + json::JsonSerializer::serialize_item(new_item) + "}");
    }
    if ((return_values == "ALL_OLD" || return_values == "UPDATED_OLD") && outcome.previous) {
        return ok("{\"Attributes\":" + json::JsonSerializer::serialize_item(*outcome.previous) + "}");
    }
    return ok("{}");
}

// Stable 64-bit FNV-1a, used to deterministically assign an item to a parallel-scan
// segment by hashing its storage key.
uint64_t fnv1a(std::string_view s) {
    uint64_t h = 1469598103934665603ULL;
    for (unsigned char c : s) { h ^= c; h *= 1099511628211ULL; }
    return h;
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

    // Scanning a secondary index reads its storage namespace; the cursor and
    // LastEvaluatedKey are then expressed over the index + base key attributes.
    std::string scan_table = std::string(name);
    const IndexSpec* idx = nullptr;
    IndexSpec idx_storage;
    std::string_view index_name;
    if (doc["IndexName"].get_string().get(index_name) == simdjson::SUCCESS) {
        idx = find_index(all_indexes(*def), index_name, idx_storage);
        if (!idx) return error(400, "ValidationException",
                               "The table does not have the specified index: " + std::string(index_name));
        scan_table = index_storage_table(std::string(name), std::string(index_name));
    }

    // Parallel scan: Segment + TotalSegments must be supplied together.
    int64_t segment = 0;
    int64_t total_segments = 0;
    const bool has_segment = doc["Segment"].get_int64().get(segment) == simdjson::SUCCESS;
    const bool has_total = doc["TotalSegments"].get_int64().get(total_segments) == simdjson::SUCCESS;
    if (has_segment != has_total) {
        return error(400, "ValidationException",
                     "The Segment and TotalSegments parameters must be provided together");
    }
    const bool segmented = has_segment && has_total;
    if (segmented) {
        if (total_segments < 1 || total_segments > 1000000) {
            return error(400, "ValidationException", "TotalSegments must be between 1 and 1000000");
        }
        if (segment < 0 || segment >= total_segments) {
            return error(400, "ValidationException", "Segment must be between 0 and TotalSegments-1");
        }
    }

    size_t limit = read_limit(doc).value_or(0);
    std::optional<std::string> start;
    if (idx) {
        simdjson::dom::element esk_el;
        if (doc["ExclusiveStartKey"].get(esk_el) == simdjson::SUCCESS) {
            if (auto esk_map = parse_attribute_map(esk_el)) start = encode_index_storage_key(*idx, *def, *esk_map);
        }
    } else {
        start = read_start_key(doc, *def);
    }

    std::string_view filter_view;
    const bool has_filter = doc["FilterExpression"].get_string().get(filter_view) == simdjson::SUCCESS;
    const std::string filter(filter_view);
    std::string_view projection_view;
    const bool has_projection = doc["ProjectionExpression"].get_string().get(projection_view) == simdjson::SUCCESS;
    NameMap names = parse_expr_names(doc);
    auto values_opt = parse_expr_values(doc);
    if (!values_opt) return error(400, "ValidationException", "Invalid ExpressionAttributeValues");

    auto storage_key = [&](const AttributeMap& item) {
        return idx ? encode_index_storage_key(*idx, *def, item) : engine::encode_primary_key(*def, item);
    };
    auto lek_json = [&](const AttributeMap& item) {
        return idx ? index_key_only_json(*idx, *def, item) : key_only_json(*def, item);
    };

    std::vector<AttributeMap> output;
    size_t scanned = 0;
    std::optional<std::string> last_evaluated;
    std::optional<std::string> last_scanned;  // key of the most recently scanned segment item

    // Segmented and non-segmented share one evaluation loop. For a segmented scan the
    // whole namespace is fetched and only the segment's keys are evaluated (pagination
    // is applied in-handler); otherwise the storage layer applies the limit/cursor.
    auto page = storage.scan(scan_table, start, segmented ? 0 : limit);
    for (const auto& item : page.items) {
        if (segmented && (fnv1a(storage_key(item)) % static_cast<uint64_t>(total_segments)) !=
                             static_cast<uint64_t>(segment)) {
            continue;  // not this segment
        }
        if (segmented && limit != 0 && scanned == limit) {
            last_evaluated = last_scanned;  // resume after the last item we actually scanned
            break;
        }
        ++scanned;
        last_scanned = lek_json(item);
        if (!idx && item_is_expired(*def, item)) continue;  // TTL applies to base-table scans
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
    if (!segmented) {
        scanned = page.items.size();
        if (page.last_evaluated_key && !page.items.empty()) last_evaluated = lek_json(page.items.back());
    }

    std::string out = "{\"Items\":" + items_to_json(output) +
                      ",\"Count\":" + std::to_string(output.size()) +
                      ",\"ScannedCount\":" + std::to_string(scanned);
    if (last_evaluated) out += ",\"LastEvaluatedKey\":" + *last_evaluated;
    out += "}";
    return ok(std::move(out));
}

ApiResult handle_index_query(engine::StorageEngine& storage,
                             const core::TableDefinition& def, const std::string& index_name,
                             simdjson::dom::element doc) {
    IndexSpec idx_storage;
    const IndexSpec* idx = find_index(all_indexes(def), index_name, idx_storage);
    if (!idx) {
        return error(400, "ValidationException",
                     "The table does not have the specified index: " + index_name);
    }
    if (idx->key_schema.empty()) {
        return error(400, "ValidationException", "Index has no key schema: " + index_name);
    }

    NameMap names = parse_expr_names(doc);
    auto values_opt = parse_expr_values(doc);
    if (!values_opt) return error(400, "ValidationException", "Invalid ExpressionAttributeValues");

    std::string_view kce_view;
    if (doc["KeyConditionExpression"].get_string().get(kce_view) != simdjson::SUCCESS) {
        return error(400, "ValidationException", "Querying an index requires KeyConditionExpression");
    }
    auto parsed = parse_key_condition(std::string(kce_view), names, *values_opt);
    if (!parsed.ok) {
        return error(400, "ValidationException", parsed.error.empty() ? "Invalid KeyConditionExpression" : parsed.error);
    }
    // The KCE partition key must be the index hash key.
    if (parsed.pk_name != idx->key_schema[0].attribute_name) {
        return error(400, "ValidationException",
                     "KeyConditionExpression partition key must be the index hash key");
    }

    const std::string storage_tbl = index_storage_table(def.table_name, index_name);
    AttributeMap pk_condition;
    pk_condition[parsed.pk_name] = parsed.pk_value;
    auto result = storage.query(storage_tbl, pk_condition, std::nullopt, 0);

    bool forward = true;
    auto sif_rc = doc["ScanIndexForward"].get_bool().get(forward); (void)sif_rc;

    std::vector<AttributeMap> matched;
    for (auto& item : result.items) {
        if (parsed.sort.present) {
            auto it = item.find(parsed.sk_name);
            if (it == item.end() || !sort_matches(it->second, parsed.sort)) continue;
        }
        matched.push_back(std::move(item));
    }
    if (!forward) std::reverse(matched.begin(), matched.end());

    // ExclusiveStartKey carries the index + base key attributes.
    size_t begin_idx = 0;
    simdjson::dom::element esk_el;
    if (doc["ExclusiveStartKey"].get(esk_el) == simdjson::SUCCESS) {
        if (auto esk_map = parse_attribute_map(esk_el)) {
            std::string esk = encode_index_storage_key(*idx, def, *esk_map);
            for (; begin_idx < matched.size(); ++begin_idx) {
                std::string ek = encode_index_storage_key(*idx, def, matched[begin_idx]);
                bool past = forward ? (ek > esk) : (ek < esk);
                if (past) break;
            }
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
            last_evaluated = index_key_only_json(*idx, def, matched[i - 1]);
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
            if (unsupported) return error(400, "ValidationException", "Document-path ProjectionExpression is not yet supported");
            output.push_back(std::move(projected));
        } else {
            output.push_back(item);
        }
    }

    std::string out = "{\"Items\":" + items_to_json(output) +
                      ",\"Count\":" + std::to_string(output.size()) +
                      ",\"ScannedCount\":" + std::to_string(examined);
    if (last_evaluated) out += ",\"LastEvaluatedKey\":" + *last_evaluated;
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

    // Querying a secondary index reads from the index's storage namespace.
    std::string_view index_name;
    if (doc["IndexName"].get_string().get(index_name) == simdjson::SUCCESS) {
        return handle_index_query(storage, *def, std::string(index_name), doc);
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
        if (item_is_expired(*def, item)) continue;  // TTL: expired items are not returned
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
                                  simdjson::dom::element doc, streams::StreamManager* streams) {
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
    if (ops.size() > 25) {
        return error(400, "ValidationException",
                     "Too many items requested for the BatchWriteItem call");
    }
    for (auto& op : ops) {
        auto def = tables.describe_table(op.table);
        const bool has_idx = def && (!def->global_secondary_indexes.empty() ||
                                     !def->local_secondary_indexes.empty());
        std::optional<AttributeMap> old;
        if (has_idx) old = storage.get(op.table, op.key);
        const bool has_stream = def && streams != nullptr && streams->is_stream_enabled(*def);
        if (!has_idx && has_stream) old = storage.get(op.table, op.key);  // need old image for the record
        if (op.is_delete) {
            storage.remove(op.table, op.key);
            if (has_idx) maintain_indexes(storage, *def, old ? &*old : nullptr, nullptr);
            if (has_stream && old) emit_stream(streams, *def, "REMOVE", extract_keys(*def, *old), old, std::nullopt);
        } else {
            storage.put(op.table, op.key, op.item);
            if (has_idx) maintain_indexes(storage, *def, old ? &*old : nullptr, &op.item);
            if (has_stream) emit_stream(streams, *def, old ? "MODIFY" : "INSERT",
                                        extract_keys(*def, op.item), old, op.item);
        }
    }
    return ok("{\"UnprocessedItems\":{}}");
}

ApiResult handle_batch_get_item(engine::TableManager& tables, engine::StorageEngine& storage,
                                simdjson::dom::element doc) {
    simdjson::dom::object request_items;
    if (doc["RequestItems"].get_object().get(request_items) != simdjson::SUCCESS) {
        return error(400, "ValidationException", "RequestItems is required");
    }
    // Enforce the AWS cap of 100 keys per BatchGetItem call (across all tables).
    size_t total_keys = 0;
    for (auto table_field : request_items) {
        simdjson::dom::array keys;
        if (table_field.value["Keys"].get_array().get(keys) == simdjson::SUCCESS) {
            total_keys += keys.size();
        }
    }
    if (total_keys > 100) {
        return error(400, "ValidationException",
                     "Too many items requested for the BatchGetItem call");
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
            if (item && !item_is_expired(*def, *item)) items.push_back(*item);
        }
        if (!first_table) responses += ",";
        responses += "\"" + table_name + "\":" + items_to_json(items);
        first_table = false;
    }
    responses += "}";
    return ok("{\"Responses\":" + responses + ",\"UnprocessedKeys\":{}}");
}

ApiResult handle_transact_write_items(engine::TableManager& tables, engine::StorageEngine& storage,
                                      simdjson::dom::element doc, streams::StreamManager* streams) {
    simdjson::dom::array transact_items;
    if (doc["TransactItems"].get_array().get(transact_items) != simdjson::SUCCESS) {
        return error(400, "ValidationException", "TransactItems is required");
    }
    if (transact_items.size() > 100) {
        return error(400, "ValidationException",
                     "Member must have length less than or equal to 100 (TransactWriteItems)");
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
        if (a.kind == 3) continue;  // ConditionCheck performs no write
        auto def = tables.describe_table(a.table);
        const bool has_idx = def && (!def->global_secondary_indexes.empty() ||
                                     !def->local_secondary_indexes.empty());
        const bool has_stream = def && streams != nullptr && streams->is_stream_enabled(*def);
        std::optional<AttributeMap> old;
        if (has_idx || has_stream) old = storage.get(a.table, a.key);
        if (a.kind == 0) {
            storage.put(a.table, a.key, a.item);
            if (has_idx) maintain_indexes(storage, *def, old ? &*old : nullptr, &a.item);
            if (has_stream) emit_stream(streams, *def, old ? "MODIFY" : "INSERT",
                                        extract_keys(*def, a.item), old, a.item);
        } else if (a.kind == 1) {
            storage.remove(a.table, a.key);
            if (has_idx) maintain_indexes(storage, *def, old ? &*old : nullptr, nullptr);
            if (has_stream && old) emit_stream(streams, *def, "REMOVE", extract_keys(*def, *old), old, std::nullopt);
        } else if (a.kind == 2) {
            storage.put(a.table, a.key, precomputed_updates[i]);
            if (has_idx) maintain_indexes(storage, *def, old ? &*old : nullptr, &precomputed_updates[i]);
            if (has_stream) emit_stream(streams, *def, old ? "MODIFY" : "INSERT",
                                        extract_keys(*def, precomputed_updates[i]), old, precomputed_updates[i]);
        }
    }
    return ok("{}");
}

ApiResult handle_transact_get_items(engine::TableManager& tables, engine::StorageEngine& storage,
                                    simdjson::dom::element doc) {
    simdjson::dom::array transact_items;
    if (doc["TransactItems"].get_array().get(transact_items) != simdjson::SUCCESS) {
        return error(400, "ValidationException", "TransactItems is required");
    }
    if (transact_items.size() > 100) {
        return error(400, "ValidationException",
                     "Member must have length less than or equal to 100 (TransactGetItems)");
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
        if (item && !item_is_expired(*def, *item))
            responses += "{\"Item\":" + json::JsonSerializer::serialize_item(*item) + "}";
        else responses += "{}";
        first = false;
    }
    responses += "]";
    return ok("{\"Responses\":" + responses + "}");
}

// ---- DynamoDB Streams data plane ----------------------------------------

std::string stream_view_type_str(core::StreamViewType v) {
    switch (v) {
        case core::StreamViewType::KEYS_ONLY: return "KEYS_ONLY";
        case core::StreamViewType::NEW_IMAGE: return "NEW_IMAGE";
        case core::StreamViewType::OLD_IMAGE: return "OLD_IMAGE";
        default: return "NEW_AND_OLD_IMAGES";
    }
}

std::string serialize_stream_record(const streams::StreamRecord& r, core::StreamViewType view) {
    std::string out = "{\"eventID\":\"" + r.event_id + "\",\"eventName\":\"" + r.event_name +
                      "\",\"eventVersion\":\"1.1\",\"eventSource\":\"aws:dynamodb\",\"dynamodb\":{";
    out += "\"ApproximateCreationDateTime\":" + std::to_string(r.approximate_creation_date_time);
    out += ",\"Keys\":" + json::JsonSerializer::serialize_item(r.keys);
    if (r.new_image) out += ",\"NewImage\":" + json::JsonSerializer::serialize_item(*r.new_image);
    if (r.old_image) out += ",\"OldImage\":" + json::JsonSerializer::serialize_item(*r.old_image);
    out += ",\"SequenceNumber\":\"" + r.sequence_number + "\"";
    out += ",\"SizeBytes\":" + std::to_string(r.size_bytes);
    out += ",\"StreamViewType\":\"" + stream_view_type_str(view) + "\"}}";
    return out;
}

std::string serialize_stream_description(const streams::StreamDescription& d) {
    std::string out = "{\"StreamArn\":\"" + d.stream_arn + "\",\"StreamLabel\":\"" + d.stream_label +
                      "\",\"StreamStatus\":\"" + d.stream_status + "\",\"StreamViewType\":\"" +
                      stream_view_type_str(d.stream_view_type) + "\",\"CreationRequestDateTime\":" +
                      std::to_string(d.creation_request_date_time) + ",\"TableName\":\"" + d.table_name +
                      "\",\"Shards\":[";
    for (size_t i = 0; i < d.shards.size(); ++i) {
        const auto& s = d.shards[i];
        if (i) out += ",";
        out += "{\"ShardId\":\"" + s.shard_id + "\",\"SequenceNumberRange\":{\"StartingSequenceNumber\":\"" +
               s.sequence_number_range.starting_sequence_number + "\"";
        if (s.sequence_number_range.ending_sequence_number) {
            out += ",\"EndingSequenceNumber\":\"" + *s.sequence_number_range.ending_sequence_number + "\"";
        }
        out += "}}";
    }
    out += "]}";
    return out;
}

ApiResult stream_error(streams::StreamError e) {
    switch (e) {
        case streams::StreamError::ResourceNotFound:
            return error(400, "ResourceNotFoundException", "Requested resource not found");
        case streams::StreamError::ExpiredIterator:
            return error(400, "ExpiredIteratorException", "The shard iterator has expired");
        case streams::StreamError::LimitExceeded:
            return error(400, "LimitExceededException", "Too many requests");
        default:
            return error(500, "InternalServerError", "Stream operation failed");
    }
}

ApiResult handle_list_streams(streams::StreamManager& streams, simdjson::dom::element doc) {
    std::optional<std::string> table_name;
    std::string_view tn;
    if (doc["TableName"].get_string().get(tn) == simdjson::SUCCESS) table_name = std::string(tn);
    std::optional<std::string> start;
    std::string_view sa;
    if (doc["ExclusiveStartStreamArn"].get_string().get(sa) == simdjson::SUCCESS) start = std::string(sa);
    size_t limit = read_limit(doc).value_or(100);

    auto res = streams.list_streams(table_name, start, limit);
    if (!res) return stream_error(res.error());
    std::string out = "{\"Streams\":[";
    for (size_t i = 0; i < res->streams.size(); ++i) {
        const auto& s = res->streams[i];
        if (i) out += ",";
        out += "{\"StreamArn\":\"" + s.stream_arn + "\",\"TableName\":\"" + s.table_name +
               "\",\"StreamLabel\":\"" + s.stream_label + "\"}";
    }
    out += "]";
    if (res->last_evaluated_stream_arn) out += ",\"LastEvaluatedStreamArn\":\"" + *res->last_evaluated_stream_arn + "\"";
    out += "}";
    return ok(std::move(out));
}

ApiResult handle_describe_stream(streams::StreamManager& streams, simdjson::dom::element doc) {
    std::string_view arn;
    if (doc["StreamArn"].get_string().get(arn) != simdjson::SUCCESS) {
        return error(400, "ValidationException", "StreamArn is required");
    }
    std::optional<std::string> start_shard;
    std::string_view ss;
    if (doc["ExclusiveStartShardId"].get_string().get(ss) == simdjson::SUCCESS) start_shard = std::string(ss);
    auto res = streams.describe_stream(arn, start_shard, read_limit(doc).value_or(100));
    if (!res) return stream_error(res.error());
    return ok("{\"StreamDescription\":" + serialize_stream_description(*res) + "}");
}

ApiResult handle_get_shard_iterator(streams::StreamManager& streams, simdjson::dom::element doc) {
    std::string_view arn;
    std::string_view shard;
    std::string_view itype;
    if (doc["StreamArn"].get_string().get(arn) != simdjson::SUCCESS ||
        doc["ShardId"].get_string().get(shard) != simdjson::SUCCESS ||
        doc["ShardIteratorType"].get_string().get(itype) != simdjson::SUCCESS) {
        return error(400, "ValidationException", "StreamArn, ShardId and ShardIteratorType are required");
    }
    std::optional<std::string> seq;
    std::string_view sn;
    if (doc["SequenceNumber"].get_string().get(sn) == simdjson::SUCCESS) seq = std::string(sn);
    auto res = streams.create_shard_iterator(arn, shard, itype, seq);
    if (!res) return stream_error(res.error());
    return ok("{\"ShardIterator\":\"" + *res + "\"}");
}

ApiResult handle_get_records(streams::StreamManager& streams, simdjson::dom::element doc) {
    std::string_view it;
    if (doc["ShardIterator"].get_string().get(it) != simdjson::SUCCESS) {
        return error(400, "ValidationException", "ShardIterator is required");
    }
    auto res = streams.get_records(it, read_limit(doc).value_or(1000));
    if (!res) return stream_error(res.error());
    std::string out = "{\"Records\":[";
    for (size_t i = 0; i < res->records.size(); ++i) {
        if (i) out += ",";
        // The record carries its own images; view type is informational here.
        out += serialize_stream_record(res->records[i], core::StreamViewType::NEW_AND_OLD_IMAGES);
    }
    out += "]";
    if (res->next_shard_iterator) out += ",\"NextShardIterator\":\"" + *res->next_shard_iterator + "\"";
    out += "}";
    return ok(std::move(out));
}

}  // namespace

namespace {

// Classifies the single-table data operations that consume capacity centrally.
bool is_read_op(Operation op) {
    return op == Operation::GetItem || op == Operation::Query || op == Operation::Scan;
}
bool is_write_op(Operation op) {
    return op == Operation::PutItem || op == Operation::UpdateItem || op == Operation::DeleteItem;
}

// ---- PartiQL ------------------------------------------------------------
// A focused PartiQL implementation for SELECT / INSERT / UPDATE / DELETE that
// reuses the storage engine plus index/stream maintenance. SELECT scans the table
// and filters in-handler; write statements address a single item by full key.

struct PqlToken { enum T { Ident, Str, Num, Param, Punct, End } t; std::string s; };

std::vector<PqlToken> pql_tokenize(const std::string& in, bool& bad) {
    std::vector<PqlToken> out;
    bad = false;
    size_t i = 0;
    while (i < in.size()) {
        char c = in[i];
        if (std::isspace(static_cast<unsigned char>(c)) != 0) { ++i; continue; }
        if (c == '\'') {
            std::string s;
            ++i;
            while (i < in.size() && in[i] != '\'') {
                if (in[i] == '\'' ) break;
                s.push_back(in[i++]);
            }
            if (i >= in.size()) { bad = true; break; }
            ++i;  // closing quote
            out.push_back({PqlToken::Str, s});
        } else if (c == '"') {  // quoted identifier (table/attr name)
            std::string s; ++i;
            while (i < in.size() && in[i] != '"') s.push_back(in[i++]);
            if (i >= in.size()) { bad = true; break; }
            ++i;
            out.push_back({PqlToken::Ident, s});
        } else if (c == '?') { out.push_back({PqlToken::Param, "?"}); ++i; }
        else if (std::isalpha(static_cast<unsigned char>(c)) != 0 || c == '_') {
            std::string s;
            while (i < in.size() && (std::isalnum(static_cast<unsigned char>(in[i])) != 0 ||
                                     in[i] == '_' || in[i] == '.')) s.push_back(in[i++]);
            out.push_back({PqlToken::Ident, s});
        } else if (std::isdigit(static_cast<unsigned char>(c)) != 0 || c == '-' ) {
            std::string s;
            while (i < in.size() && (std::isdigit(static_cast<unsigned char>(in[i])) != 0 ||
                                     in[i] == '.' || in[i] == '-' || in[i] == '+' ||
                                     in[i] == 'e' || in[i] == 'E')) s.push_back(in[i++]);
            out.push_back({PqlToken::Num, s});
        } else if (c == '<' && i + 1 < in.size() && in[i+1] == '=') { out.push_back({PqlToken::Punct, "<="}); i += 2; }
        else if (c == '>' && i + 1 < in.size() && in[i+1] == '=') { out.push_back({PqlToken::Punct, ">="}); i += 2; }
        else if (c == '<' && i + 1 < in.size() && in[i+1] == '>') { out.push_back({PqlToken::Punct, "<>"}); i += 2; }
        else if (std::string("={}[](),*<>.:").find(c) != std::string::npos) {
            out.push_back({PqlToken::Punct, std::string(1, c)}); ++i;
        } else { bad = true; break; }
    }
    out.push_back({PqlToken::End, ""});
    return out;
}

std::string pql_upper(const std::string& s) {
    std::string u = s;
    std::transform(u.begin(), u.end(), u.begin(), [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
    return u;
}

struct PqlParser {
    const std::vector<PqlToken>& toks;
    const std::vector<std::shared_ptr<core::AttributeValue>>& params;
    size_t pos = 0;
    size_t next_param = 0;
    std::string error;

    const PqlToken& peek() const { return toks[pos]; }
    const PqlToken& advance() { return toks[pos < toks.size() - 1 ? pos++ : pos]; }
    bool kw(const char* k) {
        if (peek().t == PqlToken::Ident && pql_upper(peek().s) == k) { advance(); return true; }
        return false;
    }
    bool punct(const char* p) {
        if (peek().t == PqlToken::Punct && peek().s == p) { advance(); return true; }
        return false;
    }

    std::shared_ptr<core::AttributeValue> parse_value() {
        const PqlToken& t = peek();
        if (t.t == PqlToken::Param) {
            advance();
            if (next_param >= params.size()) { error = "Not enough parameters for PartiQL statement"; return nullptr; }
            return params[next_param++];
        }
        if (t.t == PqlToken::Str) { advance(); auto v = std::make_shared<core::AttributeValue>(); v->type = core::AttributeType::S; v->value = core::String(t.s); return v; }
        if (t.t == PqlToken::Num) { advance(); auto v = std::make_shared<core::AttributeValue>(); v->type = core::AttributeType::N; v->value = core::String(t.s); return v; }
        if (t.t == PqlToken::Ident) {
            std::string u = pql_upper(t.s);
            if (u == "TRUE" || u == "FALSE") { advance(); auto v = std::make_shared<core::AttributeValue>(); v->type = core::AttributeType::BOOL; v->value = (u == "TRUE"); return v; }
            if (u == "NULL") { advance(); auto v = std::make_shared<core::AttributeValue>(); v->type = core::AttributeType::NUL; v->value = std::monostate{}; return v; }
        }
        if (t.t == PqlToken::Punct && t.s == "{") {
            advance();
            auto v = std::make_shared<core::AttributeValue>(); v->type = core::AttributeType::M;
            core::MapValue m;
            if (!(peek().t == PqlToken::Punct && peek().s == "}")) {
                while (true) {
                    if (peek().t != PqlToken::Str) { error = "Map keys must be quoted strings"; return nullptr; }
                    std::string key = advance().s;
                    if (!punct(":")) { error = "Map entry requires ':'"; return nullptr; }
                    auto val = parse_value();
                    if (!val) return nullptr;
                    m[core::String(key)] = val;
                    if (punct(",")) continue;
                    break;
                }
            }
            if (!punct("}")) { error = "Unterminated map literal"; return nullptr; }
            v->value = std::move(m);
            return v;
        }
        if (t.t == PqlToken::Punct && t.s == "[") {
            advance();
            auto v = std::make_shared<core::AttributeValue>(); v->type = core::AttributeType::L;
            core::ListValue l;
            if (!(peek().t == PqlToken::Punct && peek().s == "]")) {
                while (true) { auto e = parse_value(); if (!e) return nullptr; l.push_back(e); if (punct(",")) continue; break; }
            }
            if (!punct("]")) { error = "Unterminated list literal"; return nullptr; }
            v->value = std::move(l);
            return v;
        }
        error = "Unexpected token in PartiQL value";
        return nullptr;
    }
};

bool pql_attr_compare(const core::AttributeValue& a, const std::string& op, const core::AttributeValue& b) {
    if (a.type != b.type) return op == "<>";  // different types: only <> is true
    auto cmp_lt = [&]() -> bool {
        if (a.type == core::AttributeType::S) return std::get<core::String>(a.value) < std::get<core::String>(b.value);
        if (a.type == core::AttributeType::N) return std::strtold(std::get<core::String>(a.value).c_str(), nullptr) <
                                                     std::strtold(std::get<core::String>(b.value).c_str(), nullptr);
        return false;
    };
    bool eq = false;
    if (a.type == core::AttributeType::S || a.type == core::AttributeType::N) eq = std::get<core::String>(a.value) == std::get<core::String>(b.value);
    else if (a.type == core::AttributeType::BOOL) eq = std::get<bool>(a.value) == std::get<bool>(b.value);
    else if (a.type == core::AttributeType::NUL) eq = true;
    if (op == "=") return eq;
    if (op == "<>") return !eq;
    if (op == "<") return cmp_lt();
    if (op == ">") return !cmp_lt() && !eq;
    if (op == "<=") return cmp_lt() || eq;
    if (op == ">=") return !cmp_lt();
    return false;
}

struct PqlCond { std::string attr; std::string op; std::shared_ptr<core::AttributeValue> value; };

ApiResult execute_partiql(engine::TableManager& tables, engine::StorageEngine& storage,
                          streams::StreamManager* streams, const std::string& statement,
                          const std::vector<std::shared_ptr<core::AttributeValue>>& params) {
    bool bad = false;
    auto toks = pql_tokenize(statement, bad);
    if (bad) return error(400, "ValidationException", "Invalid PartiQL statement syntax");
    PqlParser p{toks, params, 0, 0, {}};

    auto parse_where = [&](std::vector<PqlCond>& conds) -> bool {
        if (!p.kw("WHERE")) return true;  // no WHERE
        while (true) {
            if (p.peek().t != PqlToken::Ident) { p.error = "Expected attribute in WHERE"; return false; }
            PqlCond c;
            c.attr = p.advance().s;
            if (p.peek().t != PqlToken::Punct) { p.error = "Expected operator in WHERE"; return false; }
            c.op = p.advance().s;
            c.value = p.parse_value();
            if (!c.value) return false;
            conds.push_back(std::move(c));
            if (p.kw("AND")) continue;
            break;
        }
        return true;
    };
    auto table_name_token = [&]() -> std::optional<std::string> {
        if (p.peek().t != PqlToken::Ident) { p.error = "Expected table name"; return std::nullopt; }
        return p.advance().s;
    };

    // ---- SELECT ----
    if (p.kw("SELECT")) {
        std::vector<std::string> select_list;
        if (p.punct("*")) { /* all */ }
        else {
            while (true) {
                if (p.peek().t != PqlToken::Ident) { return error(400, "ValidationException", "Invalid SELECT list"); }
                select_list.push_back(p.advance().s);
                if (p.punct(",")) continue;
                break;
            }
        }
        if (!p.kw("FROM")) return error(400, "ValidationException", "PartiQL SELECT requires FROM");
        auto tname = table_name_token();
        if (!tname) return error(400, "ValidationException", p.error);
        auto def = tables.describe_table(*tname);
        if (!def) return error(400, "ResourceNotFoundException", "Table not found: " + *tname);
        std::vector<PqlCond> conds;
        if (!parse_where(conds)) return error(400, "ValidationException", p.error);

        auto scan = storage.scan(*tname, std::nullopt, 0);
        std::string items = "[";
        bool first = true;
        for (const auto& item : scan.items) {
            if (item_is_expired(*def, item)) continue;
            bool match = true;
            for (const auto& c : conds) {
                auto it = item.find(c.attr);
                if (it == item.end() || !it->second || !pql_attr_compare(*it->second, c.op, *c.value)) { match = false; break; }
            }
            if (!match) continue;
            AttributeMap projected;
            if (select_list.empty()) projected = item;
            else for (const auto& a : select_list) { auto it = item.find(a); if (it != item.end()) projected[a] = it->second; }
            if (!first) items += ",";
            items += json::JsonSerializer::serialize_item(projected);
            first = false;
        }
        items += "]";
        return ok("{\"Items\":" + items + "}");
    }

    // ---- INSERT ----
    if (p.kw("INSERT")) {
        if (!p.kw("INTO")) return error(400, "ValidationException", "PartiQL INSERT requires INTO");
        auto tname = table_name_token();
        if (!tname) return error(400, "ValidationException", p.error);
        auto def = tables.describe_table(*tname);
        if (!def) return error(400, "ResourceNotFoundException", "Table not found: " + *tname);
        if (!p.kw("VALUE")) return error(400, "ValidationException", "PartiQL INSERT requires VALUE");
        auto val = p.parse_value();
        if (!val || val->type != core::AttributeType::M) return error(400, "ValidationException", p.error.empty() ? "INSERT VALUE must be a map" : p.error);
        AttributeMap item;
        for (const auto& [k, v] : std::get<core::MapValue>(val->value)) item[std::string(k)] = v;
        if (!item_has_all_keys(*def, item)) return error(400, "ValidationException", "INSERT item missing key attributes");
        std::string key = engine::encode_primary_key(*def, item);
        auto old = storage.get(*tname, key);
        storage.put(*tname, key, item);
        maintain_indexes(storage, *def, old ? &*old : nullptr, &item);
        emit_stream(streams, *def, old ? "MODIFY" : "INSERT", extract_keys(*def, item), old, item);
        return ok("{}");
    }

    // ---- UPDATE ----
    if (p.kw("UPDATE")) {
        auto tname = table_name_token();
        if (!tname) return error(400, "ValidationException", p.error);
        auto def = tables.describe_table(*tname);
        if (!def) return error(400, "ResourceNotFoundException", "Table not found: " + *tname);
        if (!p.kw("SET")) return error(400, "ValidationException", "PartiQL UPDATE requires SET");
        std::vector<PqlCond> sets;
        while (true) {
            if (p.peek().t != PqlToken::Ident) return error(400, "ValidationException", "Invalid SET assignment");
            PqlCond a; a.attr = p.advance().s;
            if (!p.punct("=")) return error(400, "ValidationException", "SET assignment requires '='");
            a.value = p.parse_value();
            if (!a.value) return error(400, "ValidationException", p.error);
            sets.push_back(std::move(a));
            if (p.punct(",")) continue;
            break;
        }
        std::vector<PqlCond> conds;
        if (!parse_where(conds)) return error(400, "ValidationException", p.error);
        AttributeMap key_map;
        for (const auto& c : conds) if (c.op == "=") key_map[c.attr] = c.value;
        if (!item_has_all_keys(*def, key_map)) return error(400, "ValidationException", "PartiQL UPDATE requires the full primary key in WHERE");
        std::string key = engine::encode_primary_key(*def, key_map);
        AttributeMap new_item;
        auto outcome = storage.mutate(*tname, key, [&](const AttributeMap* cur) {
            AttributeMap working = cur ? *cur : AttributeMap{};
            for (const auto& [k, v] : key_map) working[k] = v;
            for (const auto& s : sets) working[s.attr] = s.value;
            new_item = working;
            return engine::StorageEngine::Mutation{engine::StorageEngine::MutationKind::Put, working};
        });
        maintain_indexes(storage, *def, outcome.previous ? &*outcome.previous : nullptr, &new_item);
        emit_stream(streams, *def, outcome.previous ? "MODIFY" : "INSERT", extract_keys(*def, new_item), outcome.previous, new_item);
        return ok("{}");
    }

    // ---- DELETE ----
    if (p.kw("DELETE")) {
        if (!p.kw("FROM")) return error(400, "ValidationException", "PartiQL DELETE requires FROM");
        auto tname = table_name_token();
        if (!tname) return error(400, "ValidationException", p.error);
        auto def = tables.describe_table(*tname);
        if (!def) return error(400, "ResourceNotFoundException", "Table not found: " + *tname);
        std::vector<PqlCond> conds;
        if (!parse_where(conds)) return error(400, "ValidationException", p.error);
        AttributeMap key_map;
        for (const auto& c : conds) if (c.op == "=") key_map[c.attr] = c.value;
        if (!item_has_all_keys(*def, key_map)) return error(400, "ValidationException", "PartiQL DELETE requires the full primary key in WHERE");
        std::string key = engine::encode_primary_key(*def, key_map);
        auto outcome = storage.mutate(*tname, key, [&](const AttributeMap*) {
            return engine::StorageEngine::Mutation{engine::StorageEngine::MutationKind::Delete, {}};
        });
        if (outcome.previous) {
            maintain_indexes(storage, *def, &*outcome.previous, nullptr);
            emit_stream(streams, *def, "REMOVE", extract_keys(*def, *outcome.previous), outcome.previous, std::nullopt);
        }
        return ok("{}");
    }

    return error(400, "ValidationException", "Unsupported PartiQL statement");
}

ApiResult handle_execute_statement(engine::TableManager& tables, engine::StorageEngine& storage,
                                   streams::StreamManager* streams, simdjson::dom::element doc) {
    std::string_view stmt;
    if (doc["Statement"].get_string().get(stmt) != simdjson::SUCCESS) {
        return error(400, "ValidationException", "Statement is required");
    }
    std::vector<std::shared_ptr<core::AttributeValue>> params;
    simdjson::dom::array arr;
    if (doc["Parameters"].get_array().get(arr) == simdjson::SUCCESS) {
        try {
            for (auto p : arr) params.push_back(std::make_shared<core::AttributeValue>(json::JsonParser::parse_attribute_value(p)));
        } catch (const std::exception&) {
            return error(400, "ValidationException", "Invalid Parameters");
        }
    }
    return execute_partiql(tables, storage, streams, std::string(stmt), params);
}

ApiResult handle_batch_execute_statement(engine::TableManager& tables, engine::StorageEngine& storage,
                                         streams::StreamManager* streams, simdjson::dom::element doc) {
    simdjson::dom::array stmts;
    if (doc["Statements"].get_array().get(stmts) != simdjson::SUCCESS) {
        return error(400, "ValidationException", "Statements is required");
    }
    std::string responses = "[";
    bool first = true;
    for (auto s : stmts) {
        std::string_view stmt;
        if (s["Statement"].get_string().get(stmt) != simdjson::SUCCESS) {
            return error(400, "ValidationException", "Each statement requires a Statement");
        }
        std::vector<std::shared_ptr<core::AttributeValue>> params;
        simdjson::dom::array arr;
        if (s["Parameters"].get_array().get(arr) == simdjson::SUCCESS) {
            try {
                for (auto pe : arr) params.push_back(std::make_shared<core::AttributeValue>(json::JsonParser::parse_attribute_value(pe)));
            } catch (const std::exception&) { return error(400, "ValidationException", "Invalid Parameters"); }
        }
        auto r = execute_partiql(tables, storage, streams, std::string(stmt), params);
        if (!first) responses += ",";
        if (r.status == 200) {
            // SELECT returns {"Items":[...]}; surface its Items, else an empty success entry.
            responses += r.body.rfind("{\"Items\":", 0) == 0 ? r.body : std::string("{}");
        } else {
            responses += "{\"Error\":{\"Code\":\"ValidationError\",\"Message\":\"statement failed\"}}";
        }
        first = false;
    }
    responses += "]";
    return ok("{\"Responses\":" + responses + "}");
}

// ---- Backups / PITR / global tables -------------------------------------

std::string serialize_backup_details(const backups::BackupDescription& d) {
    const auto& s = d.backup_summary;
    return "{\"BackupArn\":\"" + s.backup_arn + "\",\"BackupName\":\"" + s.backup_name +
           "\",\"BackupStatus\":\"" + s.backup_status + "\",\"BackupType\":\"" + s.backup_type +
           "\",\"BackupCreationDateTime\":" + std::to_string(s.backup_creation_datetime) +
           ",\"BackupSizeBytes\":" + std::to_string(s.backup_size_bytes) + "}";
}

std::string serialize_backup_summary(const backups::BackupSummary& s) {
    return "{\"BackupArn\":\"" + s.backup_arn + "\",\"BackupName\":\"" + s.backup_name +
           "\",\"BackupStatus\":\"" + s.backup_status + "\",\"BackupType\":\"" + s.backup_type +
           "\",\"TableName\":\"" + s.table_name +
           "\",\"BackupCreationDateTime\":" + std::to_string(s.backup_creation_datetime) +
           ",\"BackupSizeBytes\":" + std::to_string(s.backup_size_bytes) + "}";
}

ApiResult handle_create_backup(engine::TableManager& tables, engine::StorageEngine& storage,
                               backups::BackupManager& backups, simdjson::dom::element doc) {
    std::string_view tname;
    std::string_view bname;
    if (doc["TableName"].get_string().get(tname) != simdjson::SUCCESS ||
        doc["BackupName"].get_string().get(bname) != simdjson::SUCCESS) {
        return error(400, "ValidationException", "TableName and BackupName are required");
    }
    auto def = tables.describe_table(tname);
    if (!def) return error(400, "ResourceNotFoundException", "Table not found: " + std::string(tname));
    auto scan = storage.scan(std::string(tname), std::nullopt, 0);
    auto desc = backups.create_backup(std::string(tname), std::string(bname), *def, scan.items);
    if (!desc) return error(500, "InternalServerError", "Failed to create backup");
    return ok("{\"BackupDetails\":" + serialize_backup_details(*desc) + "}");
}

ApiResult handle_list_backups(backups::BackupManager& backups, simdjson::dom::element doc) {
    std::string_view tname;
    std::string table = (doc["TableName"].get_string().get(tname) == simdjson::SUCCESS) ? std::string(tname) : "";
    auto summaries = backups.list_backups(table);
    std::string out = "{\"BackupSummaries\":[";
    for (size_t i = 0; i < summaries.size(); ++i) {
        if (i) out += ",";
        out += serialize_backup_summary(summaries[i]);
    }
    out += "]}";
    return ok(std::move(out));
}

ApiResult handle_describe_backup(backups::BackupManager& backups, simdjson::dom::element doc) {
    std::string_view arn;
    if (doc["BackupArn"].get_string().get(arn) != simdjson::SUCCESS) {
        return error(400, "ValidationException", "BackupArn is required");
    }
    auto desc = backups.describe_backup(std::string(arn));
    if (!desc) return error(400, "BackupNotFoundException", "Backup not found: " + std::string(arn));
    return ok("{\"BackupDescription\":{\"BackupDetails\":" + serialize_backup_details(*desc) + "}}");
}

ApiResult handle_delete_backup(backups::BackupManager& backups, simdjson::dom::element doc) {
    std::string_view arn;
    if (doc["BackupArn"].get_string().get(arn) != simdjson::SUCCESS) {
        return error(400, "ValidationException", "BackupArn is required");
    }
    auto desc = backups.describe_backup(std::string(arn));
    if (!desc) return error(400, "BackupNotFoundException", "Backup not found: " + std::string(arn));
    backups.delete_backup(std::string(arn));
    return ok("{\"BackupDescription\":{\"BackupDetails\":" + serialize_backup_details(*desc) + "}}");
}

// Recreates a table from a snapshot's definition + items.
ApiResult restore_into(engine::TableManager& tables, engine::StorageEngine& storage,
                       core::TableDefinition def, const std::string& target,
                       const std::vector<AttributeMap>& items) {
    def.table_name = target;
    auto created = tables.create_table(def);
    if (!created) {
        if (created.error() == engine::TableError::TableAlreadyExists) {
            return error(400, "TableAlreadyExistsException", "Target table already exists: " + target);
        }
        return error(500, "InternalServerError", "Failed to restore table");
    }
    for (const auto& item : items) {
        storage.put(target, engine::encode_primary_key(*created, item), item);
        maintain_indexes(storage, *created, nullptr, &item);
    }
    return ok("{\"TableDescription\":" + serialize_table_description(*created) + "}");
}

ApiResult handle_restore_table_from_backup(engine::TableManager& tables, engine::StorageEngine& storage,
                                           backups::BackupManager& backups, simdjson::dom::element doc) {
    std::string_view arn;
    std::string_view target;
    if (doc["BackupArn"].get_string().get(arn) != simdjson::SUCCESS ||
        doc["TargetTableName"].get_string().get(target) != simdjson::SUCCESS) {
        return error(400, "ValidationException", "BackupArn and TargetTableName are required");
    }
    auto snap = backups.restore_backup(std::string(arn));
    if (!snap) return error(400, "BackupNotFoundException", "Backup not found: " + std::string(arn));
    return restore_into(tables, storage, snap->description.table_metadata, std::string(target), snap->items);
}

ApiResult handle_restore_table_to_pit(engine::TableManager& tables, engine::StorageEngine& storage,
                                      simdjson::dom::element doc) {
    std::string_view source;
    std::string_view target;
    if (doc["SourceTableName"].get_string().get(source) != simdjson::SUCCESS ||
        doc["TargetTableName"].get_string().get(target) != simdjson::SUCCESS) {
        return error(400, "ValidationException", "SourceTableName and TargetTableName are required");
    }
    auto def = tables.describe_table(source);
    if (!def) return error(400, "ResourceNotFoundException", "Table not found: " + std::string(source));
    // This engine keeps no continuous change history, so PITR restores the current
    // state of the source table into the target.
    auto scan = storage.scan(std::string(source), std::nullopt, 0);
    return restore_into(tables, storage, *def, std::string(target), scan.items);
}

ApiResult handle_continuous_backups(engine::TableManager& tables, simdjson::dom::element doc, bool update) {
    std::string_view tname;
    if (doc["TableName"].get_string().get(tname) != simdjson::SUCCESS) {
        return error(400, "ValidationException", "TableName is required");
    }
    auto def = tables.describe_table(tname);
    if (!def) return error(400, "ResourceNotFoundException", "Table not found: " + std::string(tname));
    bool enabled = def->point_in_time_recovery.point_in_time_recovery_enabled;
    if (update) {
        simdjson::dom::element pit;
        if (doc["PointInTimeRecoverySpecification"].get(pit) == simdjson::SUCCESS) {
            bool e = false;
            if (pit["PointInTimeRecoveryEnabled"].get_bool().get(e) == simdjson::SUCCESS) enabled = e;
        }
    }
    std::string status = enabled ? "ENABLED" : "DISABLED";
    return ok("{\"ContinuousBackupsDescription\":{\"ContinuousBackupsStatus\":\"ENABLED\","
              "\"PointInTimeRecoveryDescription\":{\"PointInTimeRecoveryStatus\":\"" + status + "\"}}}");
}

ApiResult handle_global_table(engine::TableManager& tables, simdjson::dom::element doc, Operation op) {
    // Single-node engine: a "global table" is the local table with one replica.
    if (op == Operation::ListGlobalTables) {
        auto names = tables.list_tables();
        std::string out = "{\"GlobalTables\":[";
        for (size_t i = 0; i < names.size(); ++i) {
            if (i) out += ",";
            out += "{\"GlobalTableName\":\"" + names[i] +
                   "\",\"ReplicationGroup\":[{\"RegionName\":\"ddblocal\"}]}";
        }
        out += "]}";
        return ok(std::move(out));
    }
    std::string_view name;
    if (doc["GlobalTableName"].get_string().get(name) != simdjson::SUCCESS) {
        return error(400, "ValidationException", "GlobalTableName is required");
    }
    if (op == Operation::DescribeGlobalTable || op == Operation::UpdateGlobalTable ||
        op == Operation::CreateGlobalTable) {
        auto def = tables.describe_table(name);
        if (!def && op != Operation::CreateGlobalTable) {
            return error(400, "GlobalTableNotFoundException", "Global table not found: " + std::string(name));
        }
        return ok("{\"GlobalTableDescription\":{\"GlobalTableName\":\"" + std::string(name) +
                  "\",\"GlobalTableStatus\":\"ACTIVE\",\"ReplicationGroup\":[{\"RegionName\":\"ddblocal\"}]}}");
    }
    return error(501, "NotImplementedException", "Unsupported global table operation");
}

}  // namespace

ApiResult handle_operation(engine::TableManager& tables, engine::StorageEngine& storage,
                           Operation op, std::string_view body,
                           engine::capacity::CapacityManager* capacity,
                           streams::StreamManager* streams,
                           backups::BackupManager* backups) {
    simdjson::dom::parser parser;
    // An empty body is valid for some operations (e.g. ListTables); default to {}.
    std::string_view effective_body = body.empty() ? std::string_view("{}") : body;
    simdjson::padded_string json(effective_body);
    simdjson::dom::element doc;
    if (parser.parse(json).get(doc) != simdjson::SUCCESS) {
        return error(400, "SerializationException", "Could not parse the request body as JSON");
    }

    // Capacity throttling for single-table data ops. A table not yet registered
    // (TableNotFound) is left to the handler to reject with ResourceNotFound.
    if (capacity != nullptr && (is_read_op(op) || is_write_op(op))) {
        std::string_view tname;
        if (doc["TableName"].get_string().get(tname) == simdjson::SUCCESS) {
            std::string table(tname);
            auto consumed = is_read_op(op) ? capacity->consume_rcu(table, 1.0)
                                           : capacity->consume_wcu(table, 1.0);
            if (!consumed && consumed.error() == engine::capacity::CapacityError::ProvisionedThroughputExceeded) {
                return error(400, "ProvisionedThroughputExceededException",
                             "The level of configured provisioned throughput for the table was exceeded");
            }
        }
    }

    try {
        // Default covers recognized-but-unimplemented ops and stream ops reached
        // without a stream manager; lifecycle ops overwrite it below.
        ApiResult result = error(501, "NotImplementedException",
                                 "Operation is recognized but not yet implemented by cynamoDB: " +
                                     std::string(ApiDispatcher::to_string(op)));
        switch (op) {
            case Operation::CreateTable:        result = handle_create_table(tables, doc); break;
            case Operation::DescribeTable:      return handle_describe_table(tables, doc);
            case Operation::DeleteTable:        result = handle_delete_table(tables, storage, doc); break;
            case Operation::UpdateTable:        result = handle_update_table(tables, doc); break;
            case Operation::UpdateTimeToLive:   return handle_update_time_to_live(tables, doc);
            case Operation::DescribeTimeToLive: return handle_describe_time_to_live(tables, doc);
            case Operation::ListTables:         return handle_list_tables(tables);
            case Operation::PutItem:            return handle_put_item(tables, storage, doc, streams);
            case Operation::GetItem:            return handle_get_item(tables, storage, doc);
            case Operation::DeleteItem:         return handle_delete_item(tables, storage, doc, streams);
            case Operation::UpdateItem:         return handle_update_item(tables, storage, doc, streams);
            case Operation::Scan:               return handle_scan(tables, storage, doc);
            case Operation::Query:              return handle_query(tables, storage, doc);
            case Operation::BatchWriteItem:     return handle_batch_write_item(tables, storage, doc, streams);
            case Operation::BatchGetItem:       return handle_batch_get_item(tables, storage, doc);
            case Operation::TransactWriteItems: return handle_transact_write_items(tables, storage, doc, streams);
            case Operation::TransactGetItems:   return handle_transact_get_items(tables, storage, doc);
            case Operation::ExecuteStatement:   return handle_execute_statement(tables, storage, streams, doc);
            case Operation::BatchExecuteStatement: return handle_batch_execute_statement(tables, storage, streams, doc);
            case Operation::ListStreams:
                if (streams) return handle_list_streams(*streams, doc);
                break;
            case Operation::DescribeStream:
                if (streams) return handle_describe_stream(*streams, doc);
                break;
            case Operation::GetShardIterator:
                if (streams) return handle_get_shard_iterator(*streams, doc);
                break;
            case Operation::GetRecords:
                if (streams) return handle_get_records(*streams, doc);
                break;
            case Operation::CreateBackup:
                if (backups) return handle_create_backup(tables, storage, *backups, doc);
                break;
            case Operation::ListBackups:
                if (backups) return handle_list_backups(*backups, doc);
                break;
            case Operation::DescribeBackup:
                if (backups) return handle_describe_backup(*backups, doc);
                break;
            case Operation::DeleteBackup:
                if (backups) return handle_delete_backup(*backups, doc);
                break;
            case Operation::RestoreTableFromBackup:
                if (backups) { result = handle_restore_table_from_backup(tables, storage, *backups, doc); break; }
                break;
            case Operation::RestoreTableToPointInTime:
                result = handle_restore_table_to_pit(tables, storage, doc); break;
            case Operation::UpdateContinuousBackups:  return handle_continuous_backups(tables, doc, true);
            case Operation::DescribeContinuousBackups: return handle_continuous_backups(tables, doc, false);
            case Operation::CreateGlobalTable:
            case Operation::DescribeGlobalTable:
            case Operation::UpdateGlobalTable:
            case Operation::ListGlobalTables:         return handle_global_table(tables, doc, op);
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

        // Keep the capacity manager and stream manager in sync with table lifecycle.
        if (result.status == 200) {
            std::string_view tname;
            if (doc["TableName"].get_string().get(tname) == simdjson::SUCCESS) {
                if (op == Operation::CreateTable || op == Operation::UpdateTable) {
                    auto def = tables.describe_table(tname);
                    if (def) {
                        if (capacity != nullptr) capacity->register_table(*def);
                        if (streams != nullptr) streams->sync_table(*def);
                    }
                } else if (op == Operation::DeleteTable) {
                    if (capacity != nullptr) capacity->unregister_table(std::string(tname));
                    if (streams != nullptr) streams->remove_table(tname);
                }
            }
            // Restore operations create a brand-new table under TargetTableName.
            std::string_view target;
            if ((op == Operation::RestoreTableFromBackup || op == Operation::RestoreTableToPointInTime) &&
                doc["TargetTableName"].get_string().get(target) == simdjson::SUCCESS) {
                auto def = tables.describe_table(target);
                if (def) {
                    if (capacity != nullptr) capacity->register_table(*def);
                    if (streams != nullptr) streams->sync_table(*def);
                }
            }
        }
        return result;
    } catch (const std::exception& e) {
        return error(400, "ValidationException", std::string("Request could not be processed: ") + e.what());
    }
}

}  // namespace cynamodb::api
