#include <cynamodb/json/serializer.hpp>
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
    else if (key == "B") { val.type = core::AttributeType::B; /* Base64 decode placeholder */ val.value = std::pmr::vector<uint8_t>{}; }
    else if (key == "SS") { val.type = core::AttributeType::SS; val.value = core::StringSet{}; }
    else if (key == "NS") { val.type = core::AttributeType::NS; val.value = core::NumberSet{}; }
    else if (key == "BS") { val.type = core::AttributeType::BS; val.value = core::BinarySet{}; }
    else throw std::invalid_argument("Unsupported type key");
    
    return val;
}

core::TableDefinition JsonParser::parse_table_definition([[maybe_unused]] simdjson::dom::element el) {
    return {};
}

std::string JsonSerializer::serialize_attribute_value(const core::AttributeValue& val) {
    core::String out;
    if (val.type == core::AttributeType::S) out = "{\"S\":\"" + escape_json(std::get<core::String>(val.value)) + "\"}";
    else if (val.type == core::AttributeType::N) out = "{\"N\":\"" + std::get<core::String>(val.value) + "\"}";
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
