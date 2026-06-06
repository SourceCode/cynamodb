#include <cynamodb/json/serializer.hpp>
#include <cynamodb/utils/base64.hpp>
#include <algorithm>
#include <array>
#include <cctype>
#include <chrono>
#include <expected>
#include <limits>
#include <optional>
#include <stdexcept>
#include <unordered_set>

namespace cynamodb::json {

namespace {

thread_local simdjson::ondemand::parser parser;

core::String escape_json(std::string_view input) {
    core::String out;
    out.reserve(input.size());
    static constexpr char kHex[] = "0123456789ABCDEF";
    for (unsigned char c : input) {
        if (c == '"') out += "\\\"";
        else if (c == '\\') out += "\\\\";
        else if (c == '\b') out += "\\b";
        else if (c == '\f') out += "\\f";
        else if (c == '\n') out += "\\n";
        else if (c == '\r') out += "\\r";
        else if (c == '\t') out += "\\t";
        else if (c < 0x20) {
            out += "\\u00";
            out.push_back(kHex[(c >> 4U) & 0x0FU]);
            out.push_back(kHex[c & 0x0FU]);
        } else {
            out.push_back(static_cast<char>(c));
        }
    }
    return out;
}

size_t saturating_add(size_t a, size_t b) {
    if (std::numeric_limits<size_t>::max() - a < b) return std::numeric_limits<size_t>::max();
    return a + b;
}

} // namespace

core::AttributeValue JsonParser::parse_attribute_value(simdjson::dom::element el) {
    core::AttributeValue val;
    simdjson::dom::object obj = el.get_object();
    if (obj.size() != 1) throw std::invalid_argument("AttributeValue must contain exactly one type key");
    
    auto it = obj.begin();
    std::string_view key = it.key();
    if (key == "S") { val.type = core::AttributeType::S; val.value = core::String(it.value().get_string().value()); }
    else if (key == "N") { val.type = core::AttributeType::N; val.value = core::String(it.value().get_string().value()); }
    else if (key == "BOOL") { val.type = core::AttributeType::BOOL; val.value = it.value().get_bool().value(); }
    else if (key == "NULL") { val.type = core::AttributeType::NUL; val.value = std::monostate{}; }
    else if (key == "M") {
        val.type = core::AttributeType::M;
        core::MapValue map;
        for (auto field : it.value().get_object()) {
            map[core::String(field.key)] = std::make_shared<core::AttributeValue>(parse_attribute_value(field.value));
        }
        val.value = std::move(map);
    }
    else if (key == "L") {
        val.type = core::AttributeType::L;
        core::ListValue list;
        for (auto elem : it.value().get_array()) {
            list.push_back(std::make_shared<core::AttributeValue>(parse_attribute_value(elem)));
        }
        val.value = std::move(list);
    }
    else if (key == "B") {
        val.type = core::AttributeType::B;
        auto decoded = utils::base64_decode(it.value().get_string().value());
        if (!decoded) throw std::invalid_argument("B attribute value is not valid base64");
        val.value = std::pmr::vector<uint8_t>(decoded->begin(), decoded->end());
    }
    else if (key == "SS") {
        val.type = core::AttributeType::SS;
        core::StringSet set;
        for (auto elem : it.value().get_array()) {
            set.values.push_back(core::String(elem.get_string().value()));
        }
        val.value = std::move(set);
    }
    else if (key == "NS") {
        val.type = core::AttributeType::NS;
        core::NumberSet set;
        for (auto elem : it.value().get_array()) {
            set.values.push_back(core::String(elem.get_string().value()));
        }
        val.value = std::move(set);
    }
    else if (key == "BS") {
        val.type = core::AttributeType::BS;
        core::BinarySet set;
        for (auto elem : it.value().get_array()) {
            auto decoded = utils::base64_decode(elem.get_string().value());
            if (!decoded) throw std::invalid_argument("BS attribute value is not valid base64");
            set.values.emplace_back(decoded->begin(), decoded->end());
        }
        val.value = std::move(set);
    }
    else throw std::invalid_argument("Unsupported type key");
    
    return val;
}

namespace {

core::KeyType parse_key_type(std::string_view s) {
    return s == "RANGE" ? core::KeyType::RANGE : core::KeyType::HASH;
}

std::optional<core::AttributeType> parse_attribute_type(std::string_view s) {
    if (s == "S") return core::AttributeType::S;
    if (s == "N") return core::AttributeType::N;
    if (s == "B") return core::AttributeType::B;
    return std::nullopt;
}

}  // namespace

core::TableDefinition JsonParser::parse_table_definition(simdjson::dom::element el) {
    core::TableDefinition def;

    std::string_view name;
    if (el["TableName"].get_string().get(name) == simdjson::SUCCESS) {
        def.table_name = std::string(name);
    }

    simdjson::dom::array key_schema;
    if (el["KeySchema"].get_array().get(key_schema) == simdjson::SUCCESS) {
        for (auto elem : key_schema) {
            std::string_view attr_name;
            std::string_view key_type;
            if (elem["AttributeName"].get_string().get(attr_name) == simdjson::SUCCESS &&
                elem["KeyType"].get_string().get(key_type) == simdjson::SUCCESS) {
                def.key_schema.push_back({std::string(attr_name), parse_key_type(key_type)});
            }
        }
    }

    simdjson::dom::array attr_defs;
    if (el["AttributeDefinitions"].get_array().get(attr_defs) == simdjson::SUCCESS) {
        for (auto elem : attr_defs) {
            std::string_view attr_name;
            std::string_view attr_type;
            if (elem["AttributeName"].get_string().get(attr_name) == simdjson::SUCCESS &&
                elem["AttributeType"].get_string().get(attr_type) == simdjson::SUCCESS) {
                if (auto t = parse_attribute_type(attr_type)) {
                    def.attribute_definitions[std::string(attr_name)] = *t;
                }
            }
        }
    }

    std::string_view billing;
    if (el["BillingMode"].get_string().get(billing) == simdjson::SUCCESS && billing == "PROVISIONED") {
        def.billing_mode = core::BillingMode::PROVISIONED;
    }

    return def;
}

std::string JsonSerializer::serialize_attribute_value(const core::AttributeValue& val) {
    core::String out;
    if (val.type == core::AttributeType::S) out = "{\"S\":\"" + escape_json(std::get<core::String>(val.value)) + "\"}";
    else if (val.type == core::AttributeType::N) out = "{\"N\":\"" + escape_json(std::get<core::String>(val.value)) + "\"}";
    else if (val.type == core::AttributeType::BOOL) out = core::String("{\"BOOL\":") + (std::get<bool>(val.value) ? "true" : "false") + "}";
    else if (val.type == core::AttributeType::NUL) out = "{\"NULL\":true}";
    else if (val.type == core::AttributeType::M) {
        out = "{\"M\":{";
        bool first = true;
        for (const auto& [k, v] : std::get<core::MapValue>(val.value)) {
            if (!first) out += ",";
            out += "\"" + escape_json(k) + "\":" + core::String(serialize_attribute_value(*v));
            first = false;
        }
        out += "}}";
    }
    else if (val.type == core::AttributeType::L) {
        out = "{\"L\":[";
        bool first = true;
        for (const auto& v : std::get<core::ListValue>(val.value)) {
            if (!first) out += ",";
            out += core::String(serialize_attribute_value(*v));
            first = false;
        }
        out += "]}";
    }
    else if (val.type == core::AttributeType::B) {
        out = "{\"B\":\"" +
              core::String(utils::base64_encode_bytes(std::get<std::pmr::vector<uint8_t>>(val.value))) +
              "\"}";
    }
    else if (val.type == core::AttributeType::SS) {
        out = "{\"SS\":[";
        bool first = true;
        for (const auto& s : std::get<core::StringSet>(val.value).values) {
            if (!first) out += ",";
            out += "\"" + escape_json(s) + "\"";
            first = false;
        }
        out += "]}";
    }
    else if (val.type == core::AttributeType::NS) {
        out = "{\"NS\":[";
        bool first = true;
        for (const auto& s : std::get<core::NumberSet>(val.value).values) {
            if (!first) out += ",";
            out += "\"" + escape_json(s) + "\"";  // escaped: N values are not validated as numeric on ingest
            first = false;
        }
        out += "]}";
    }
    else if (val.type == core::AttributeType::BS) {
        out = "{\"BS\":[";
        bool first = true;
        for (const auto& b : std::get<core::BinarySet>(val.value).values) {
            if (!first) out += ",";
            out += "\"" + core::String(utils::base64_encode_bytes(b)) + "\"";
            first = false;
        }
        out += "]}";
    }
    else out = "{\"NULL\":true}";
    return std::string(out);
}

std::string JsonSerializer::serialize_item(const std::map<std::string, std::shared_ptr<core::AttributeValue>, core::StringViewLess>& item) {
    core::String out = "{";
    bool first = true;
    for (const auto& [k, v] : item) {
        if (!first) out += ",";
        out += "\"" + escape_json(k) + "\":";
        out += core::String(serialize_attribute_value(*v));
        first = false;
    }
    out += "}";
    return std::string(out);
}

std::string JsonSerializer::serialize_error(const std::string& type, const std::string& message) {
    core::String out = "{\"__type\":\"" + escape_json(type) + "\",\"message\":\"" + escape_json(message) + "\"}";
    return std::string(out);
}

size_t JsonSerializer::calculate_item_size(const std::map<std::string, std::shared_ptr<core::AttributeValue>, core::StringViewLess>& item) {
    size_t size = 0;
    for (const auto& [k, v] : item) {
        size = saturating_add(size, k.size());
        if (v) size = saturating_add(size, calculate_attr_size(*v));
    }
    return size;
}

size_t JsonSerializer::calculate_attr_size(const core::AttributeValue& val) {
    if (val.type == core::AttributeType::S || val.type == core::AttributeType::N) return std::get<core::String>(val.value).size();
    if (val.type == core::AttributeType::BOOL || val.type == core::AttributeType::NUL) return 1;
    return 0;
}

} // namespace cynamodb::json
